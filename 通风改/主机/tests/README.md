# 主机电脑侧回归测试

- MQ-2同步扩展真实协议/runtime/LoRa UART测试：18/26/34字节及角色flags矩阵、47字节CRC/分包、四字段共同有效与边界、接收事件延迟、汇总入队失败后年龄不刷新、UART排队/失败重试累计原始年龄、2秒失效及tick回绕。旧从机BME/声学兼容保持，MQ-2填FFFF。

- `test_master_rain.c`编译真实`App/master_rain.c`，仅替换HAL GPIO，检查只初始化PA11输入上拉、低有效、200/3000 ms边界、首次未确认FF、短脉冲、双向抖动及tick回绕；不加入Keil测试桩，不证明实板接线。
- 在本目录、已有build及临时MinGW PATH中运行：`gcc -std=c99 -Wall -Wextra -Werror -Istubs -I../App test_master_rain.c ../App/master_rain.c -o build/test_master_rain.exe`，随后运行`./build/test_master_rain.exe`。runtime测试默认雨滴替身未知，额外检查0/1上传及从机离线不清除本地主机雨滴。控制室现有透传测试检查26字节偏移17的0/1/FF原样传递。

- 四路风机回归编译真实`Bsp/fan_pwm.c`，HAL替身记录GPIO初始化及TIM2/3/4寄存器。验证1=PB1、2=PB8、3=PA1、4=PB9、25 kHz与上电停止、独立占空比及非法输入不改输出；不能证明实板波形或实际转速。
- 在已有`build/`与临时MinGW PATH中执行：`gcc -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_fan_pwm.c ../Bsp/fan_pwm.c -o build/test_fan_pwm.exe`，随后运行`./build/test_fan_pwm.exe`。BME/runtime测试继续覆盖四路本地执行及原流水号ACK。

- 四路扩展沿用本目录通信测试：编号1..4必须在runtime和发送队列中原样保留，0/5拒收；ACK关联及唯一无线事务保持，四路动作并行由从机维护。

- 本目录仅放 `test_*.c` 和测试说明；`stubs/` 仅放测试 HAL 替身，不能加入 Keil 固件工程或固件 include path。
- 测试编译真实 `App/master_bme280.c`、`App/master_runtime.c`，替换传感器寄存器访问、灯带、风扇和队列接口，验证状态机与通信行为；光敏测试单独编译真实 `App/master_light_control.c`，只替换 HAL GPIO 和 WS2812 发送，不宣称验证真实硬件。
- 编译结果放本目录 `build/`（git 忽略），不提交可执行文件；无清理需要时不删除文件。
- 在本目录运行下列 PowerShell 命令。PATH 仅临时作用于当前进程，确保 MinGW 编译器子进程能加载自身 DLL，不修改系统设置。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$taskCompilerPath = $env:PATH
try {
    $env:PATH = 'D:/codeblocks/MinGW/bin;' + $taskCompilerPath
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Core/Inc -I../App -I../Bsp test_master_bme280.c ../App/master_bme280.c ../App/master_runtime.c ../App/master_ingress.c -o build/test_master_bme280.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & './build/test_master_bme280.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
} finally {
    $env:PATH = $taskCompilerPath
}
```
- 测试涵盖转换等待、正负温度舍入和量程边界、缓存过期、总线失败与重试、转换超时、时钟回绕、强制采样、占位遥测以及采样期间风扇 ACK；另以虚拟时钟模拟 5 分钟运行和中途断开/恢复。实际 I²C/LoRa/灯带仍需用户烧写后单独验收，虚拟测试不能替代真实连续运行。
- 双 BME 联调测试直接使用真实 ingress 地址筛选，覆盖主从字段分离、实际发送后计时、从机超时仍返回主机、通信在线但传感器无效、重复/错流水号回复，以及排队超时后取消旧请求。

## 单事务窗口控制回归

- 在已有三个通信测试中扩展 `SET_WINDOW=0x11`，载荷为 `[1,action]`，仅动作0/1；协议测试检查控制室→主机→从机方向、窗口ACK状态0–3及禁止从机风机ACK。
- `test_master_bme280.c` 运行真实runtime，覆盖独立上/下游flow、仅真实从机确认后回成功、失败/忙/超时、500 ms实际发送窗口、1000 ms排队超时、错flow/命令/地址/布局、最终ACK入队失败保留结果、遥测/窗口互斥与时间回绕。窗口操作不得触发BME强制采样或增加遥测超时计数，风机本地执行仍保留。
- `test_master_lora_tx.c` 检查新窗口帧的过期取消、UART失败后重试及成功通知，不能在事务已经超时后迟发；保留原遥测TTL与半双工用例。沿用下方已有编译命令，不新增测试目录；这些假UART/时间测试不能证明射频送达或舵机实际到位。

## 声学协议与发送寿命回归

- `test_master_protocol.c` 编译真实协议、流式parser和本地队列；覆盖18/26、CRC、左右u32、非法布局、分包粘包、遥测入队寿命和tick回绕。
- 雨滴协议回归在该测试内验证26字节主机flags=0B/0F、偏移17的0/1/FF逐字节编码/解码保留，以及仅篡改雨滴字节时CRC拒收；不改变已有18字节历史布局用例。
- `test_master_lora_tx.c` 编译真实 `Bsp/lora.c`、协议、parser及队列，仅替换HAL UART/时间、transport和业务窗口接口，验证出队到实际UART失败重试之间不刷新遥测寿命、ACK不被此TTL误丢；`stubs/main.h`、`gpio.h`、`usart.h`仅用于这个电脑测试。
- 已有BME/runtime测试继续覆盖legacy从机，并新增扩展从机、声学失效/有效零值/大整数、事件延迟、汇总排队失败、错地址和flow。
- 下列命令也在本目录、临时MinGW PATH内执行，不加入Keil：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App test_master_protocol.c ../App/lora_protocol.c ../App/lora_stream_parser.c ../App/master_queues.c -o build/test_master_protocol.exe
& './build/test_master_protocol.exe'
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_master_lora_tx.c ../Bsp/lora.c ../App/lora_protocol.c ../App/lora_stream_parser.c ../App/master_queues.c ../App/master_ingress.c -o build/test_master_lora_tx.exe
& './build/test_master_lora_tx.exe'
```

## PA7 光敏控制灯带回归

- `test_master_light_control.c` 覆盖上电全黑、PA7 高暗/低亮、双向 1000 ms 稳定确认、短脉冲和重复状态、发送失败限频重试、目标变化取消旧重试，以及毫秒 tick 回绕。
- 使用与上文相同的临时 MinGW PATH，在本目录编译和执行；不得将测试 HAL GPIO 桩加入 Keil 固件。

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_master_light_control.c ../App/master_light_control.c -o build/test_master_light_control.exe
& './build/test_master_light_control.exe'
```
