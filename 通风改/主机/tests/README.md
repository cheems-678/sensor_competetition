# 主机电脑侧回归测试

- MAX4466测试新增test_master_acoustic.c（真实短窗服务，假ADC块/时间）和test_max4466_adc.c（真实BSP、专用平台替身）；检查引脚/秩序/ADC时钟/DMA、最新值下降与有效0、全五路同窗、缺口/驱动故障/恢复/年龄与tick回绕。新测试仍仅放tests、stubs，产物仅build；不加入Keil。原协议/runtime/UART测试扩展46字节73/77、300ms年龄、从机离线仍有本地声音及旧42输入隔离，不缩减其他传感器和控制回归。

在本目录并沿用下方临时MinGW PATH，新增测试命令：
```powershell
gcc -std=c99 -Wall -Wextra -Werror -I../App -I../Bsp test_master_acoustic.c ../App/master_acoustic.c -o build/test_master_acoustic.exe
./build/test_master_acoustic.exe
gcc -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_max4466_adc.c ../Bsp/max4466_adc.c -o build/test_max4466_adc.exe
./build/test_max4466_adc.exe
```

ADC平台桩额外注入复制期间边界切换，验证不返回覆盖中的块；逐项注入时钟/DMA/ADC/通道/校准/启动失败及DMA错误，失败不进入全板Error_Handler。测试不证明真实波形、增益或模块存在。

- 通信回归增加真实Keil栈预算检查：`node check_keil_stack.cjs`使用本工程1024字节启动栈，至少256字节中断余量；明确拒绝原1032字节调用图。三板同时验证无网页动作、查询/网页动作交错、连续五分钟上位机遥测及主机广播持续，不仅检查动作ACK。
- 热点真实控制优化回归：遥测竞争只保存一条网页命令并优先排空、2000 ms期限/回绕、重复/冲突、已有窗口不抢占、第二条不同请求busy、过期不执行；三板链路核对真实PWM入口及ACK匹配和五分钟并行运行，固件不改线格式。
- 热点控制台新增12/21方向/载荷/CRC、runtime网页来源及同flow跨来源隔离、4风机/窗口、重复/冲突/busy与丢ACK检查；真实UART验证21不被当窗口请求、不启动接收事务，发送队列8秒过期避免卡住原控制。控制室三板整链路新增实际网页控制与5分钟并行运行。原风机在遥测/窗口busy期间改为返回已有ERROR_BUSY，不执行；测试按用户批准仲裁规则更新。
- 2026-10-06扩展现有协议/runtime/UART测试与真实三板模拟：42字节测距布局、旧18/26/34兼容、独立哨兵、范围/脉宽/年龄、接收与汇总延迟、队列重试原基准、CRC与tick回绕；原功能回归不缩减。产物仅沿现有build/。

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
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Core/Inc -I../App -I../Bsp test_master_bme280.c ../App/master_bme280.c ../App/master_runtime.c ../App/master_ingress.c ../App/lora_protocol.c -o build/test_master_bme280.exe
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
# 主机状态下发回归补充

- 新03状态按共同协议校验：真实runtime快照测试覆盖无控制室查询、周期/忙碌、源有效性与各状态；真实LoRa发送测试覆盖业务优先、接收半帧/静默保护、UART失败不阻塞控制及时间回绕。原BME/雨滴/光敏/四路风机/窗口/协议/发送回归保持。共用头使用 `-I../..`，产物沿用build。
