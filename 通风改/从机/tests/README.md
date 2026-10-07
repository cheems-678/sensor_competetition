# 从机电脑侧测试约定

- 2026-10-07热点同步上位机：扩展状态/JSON测试覆盖26字节旧版及42字节声学新版、广播时源300ms校验及网页窗口累计2000ms独立失效/重复/回绕；网页新增check_web_monitoring.cjs沿现有VM模拟真实脚本，验证手动声学阈值、趋势/预警、报告、离线/重启及确认状态。浏览器仍只用本机模拟服务，截图放build；不自动清理或操作硬件。

- 真实控制优化回归：长User-Agent/未知头、关键头超长/重复、跨IPD CRLF/正文分包、来源/总长度/参数拒绝和JSON phase/reason；控制服务的排队/发送/超时/设备拒绝与有限诊断；网页不把HTTP拒绝或受理误标执行成功，仍单一HTTP在途且不重发动作。
- 热点控制台：新test_slave_web_control.c检查准入/去重/冲突/超时/回绕；所有链接slave_protocol_runtime或slave_esp_web的测试增加App/slave_web_control.c。网页生成头为gzip，ESP测试写build/http_page.gz并按真实字节数量验证Content-Length，Node解压核对HTML完全一致。现有网页测试同时检查8页导航/模型缩放/滑块键释放/单一HTTP调度/控制结果/过期草稿取消/10分钟1200点历史；浏览器只使用本机模拟服务。
- 2026-10-06在test_slave_bme280.c扩展42字节遥测与测距服务替身：有效样本、故障代次、源年龄2秒、重复回复、UART重试、回绕，真实驱动/采样服务仍由现有HC-SR04测试负责。整帧55字节/flags38，原MQ和声学位置保持。

- HC-SR04测试沿用现有目录，`test_hcsr04_timer.c`通过`stubs/hcsr04_timer_platform.h`模拟寄存器和中断，编译真实BSP；`test_slave_hcsr04.c`通过假捕获结果验证真实采样服务。ESP/API测试增加ultrasonic独立失效、原始/滤波值及容量边界；网页回归验证测距2秒时效、失败立即清空及从机重启。不安装依赖、不读取私人口令、不删除测试产物。命令与既有测试相同，分别链接`../Bsp/hcsr04_timer.c`和`../App/slave_hcsr04.c`，使用`-Istubs -I../Bsp`或`-I../App -I../Bsp`。

- 全网页防闪动回归覆盖主/从机标签两秒防抖及恢复、相同值不重写DOM、从机温湿压/MQ/窗口短暂无效和HTTP失败保留、各来源5秒硬过期、无效/重复响应不续时、曲线缺失留空、从机重启清理旧缓存与串行轮询。固件侧API有效期不放宽。
- 主机网页防闪动回归：模拟2秒边界与下一次轮询交错、短暂无效/请求失败后恢复、重复无效响应不能续时、BME与工作状态独立保留、原始年龄及HTTP耗时累计至5秒清空；从机原2秒失效行为保持。
- 主机汇总新增 `test_slave_master_status.c` 验证真实缓存/线格式的边界、独立过期、重复/逆序/重启/回绕；原runtime测试检查CRC/地址、忙碌/窗口ACK并行隔离。ESP/API测试链接 `../App/slave_master_status.c`，共用头增加 `-I../..`，覆盖JSON最坏长度、主机离线仍有本地数据、四窗口实际诊断。`generate_slave_web_page.cjs` 从App HTML生成ASCII/八进制C头，网页脚本测试检查源与头一致并覆盖独立过期/断线；浏览器仅用本机模拟数据，不向硬件发控制。
- MQ-2链路同步扩展现有BME/runtime测试：34字节/47字节整帧、有效零值及4095、重复请求源时间不刷新、发送前2秒失效、UART失败后ADC故障代次作废以及tick回绕；MQ采集使用替身，真实驱动/服务仍由各自测试负责。既有声学26字节输出断言升级为34字节，位置18/22保持。

- ESP密码版构建需 `build/esp_ap_config.local.h` 中的假口令宏 `SLAVE_ESP_AP_PASSWORD`，从 `../App/esp_ap_config.example.h` 复制示例用于测试即可；该文件被Git忽略。下方ESP GCC命令增加 `-Ibuild`，测试不读取设备私人配置。状态机检查现代/旧版两条CWSAP命令均为WPA2，发送的口令与测试配置一致，不打印口令。
- MQ-2新增测试：`test_slave_mq2.c` 用假 ADC 跑真实低速采样服务，检查启动/转换失败、超时恢复、源时间/序号、过期/回绕、0/4095与电压换算；`test_mq2_adc.c` 用HAL替身跑真实硬件驱动，检查PA7模拟输入、ADC1通道7、12 MHz时钟、长采样和非阻塞完成。ESP测试增加MQ/BME独立有效性与JSON容量边界；网页脚本检查MQ失效/过期/断线恢复。只证明软件，不证明模块存在或预热完成。

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../App -I../Bsp test_slave_mq2.c ../App/slave_mq2.c -o build/test_slave_mq2.exe
& './build/test_slave_mq2.exe'
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_mq2_adc.c ../Bsp/mq2_adc.c -o build/test_mq2_adc.exe
& './build/test_mq2_adc.exe'
```

- `test_slave_esp_web.c` 使用假异步 UART 和 BME 缓存运行真实 `slave_esp_web.c`，验证 AT 配置与兼容回退、IPD/HTTP 分包、连接隔离、发送及故障恢复、采样有效性和舵机业务停止；不能替代 ESP 实板测试。`check_slave_web_page.cjs` 检查真实内置网页的脚本、非并发轮询和缺失数据状态。测试使用现有 GCC/Node，不安装依赖。
- `test_esp_at_uart.c` 使用 `stubs/usart.h` 验证真实UART环形缓存、溢出、异步TX和错误重新挂接。`preview_slave_web_page.cjs` 仅在 `127.0.0.1:8766` 用模拟数据预览真实内置网页，不连接硬件；退出 Node 后停止服务，截图及提取的 HTML 放 `build/`。网页C常量使用UTF-8八进制字节，兼容Keil旧编译器的本机字符编码。

```powershell
Copy-Item -LiteralPath '../App/esp_ap_config.example.h' -Destination 'build/esp_ap_config.local.h'
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Ibuild -I../App -I../Bsp test_slave_esp_web.c ../App/slave_esp_web.c ../App/slave_master_status.c ../App/slave_web_control.c ../App/slave_servo_test.c -o build/test_slave_esp_web.exe
& './build/test_slave_esp_web.exe'
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_esp_at_uart.c ../Bsp/esp_at_uart.c -o build/test_esp_at_uart.exe
& './build/test_esp_at_uart.exe'
node check_slave_web_page.cjs
```

- 四路扩展测试在现有测试文件中覆盖编号1..4、不同方向/不同截止时间并行、重复不续时、反向隔离、回绕、逐路配置和失败隔离；BME/runtime测试覆盖所有编号路由及跨编号待ACK冲突。PWM寄存器测试test_servo_pwm.c用stubs/tim.h专用HAL替身编译真实驱动，检查共享定时器不复位、CCR独立及故障停机；产物沿用build/，不加入Keil，不替代实板波形验收。

- `test_*.c` 测试真实 BME 驱动、I2C 恢复流程、裸机采样与 LoRa 服务，以及真实声学 PCM/RMS 算法；`stubs/` 为电脑侧 HAL 替身，不能加入 Keil 工程。
- `test_slave_servo_test.c` 使用假 PWM 接口运行真实历史定位状态机和连续旋转型业务定时控制。业务测试覆盖停止脉宽启动、300 ms边界、到期只写一次停止脉宽、同方向在途重复不续时、反方向覆盖重新计时、停止后可重新动作、tick回绕/长停顿以及启动/运行/自动停止失败的停机保护；旧自动序列、20 ms限速、停留和独立模式仍回归。不证明真实PWM波形、精确转动时长、停止点或机械角度。
- `test_sg90_standalone_test.c` 使用假 `HAL_Delay` 与 PWM 接口运行真实独立单轮动作，覆盖重复往返、每步先等 2 秒的调用顺序、1300–1700 us 范围、四个位置的更新失败立即返回，以及替身时间计数跨越回绕。PWM 只由 `main` 启动一次，单轮动作不启动、停机或重试；测试不验证 `main` 硬件初始化、真实 HAL 延时、PWM 波形或实物运动。
- 测试结果只证明软件行为，不能代替实际 BME 应答、无线链路、上拉/供电、麦克风时钟/位对齐验收。
- 声学服务覆盖最新完整窗口的高低变化、零值覆盖、左右同窗、重复读取不消费/续时、部分窗口不替换完整快照、300 ms失效、故障/不连续/填充错误作废以及失效代次；不再测试或保留1秒最大值。BME/runtime测试用声学快照替身检查26字节回复、32位声音、发送前过期、重复缓存和动态CRC。通信测试不冒充真实DMA采集测试。
- BME/runtime 测试链接真实舵机业务控制与假 PWM，额外覆盖 `0x11 [1, action]` 的严格地址/载荷校验、ACK 独立槽及优先级、待发重复不执行、忙时不覆盖、20 ms 预加载与 50 ms 接收静默、有限 UART 重试、时间回绕，以及遥测/强制采样和重复缓存不被窗口命令破坏。ACK 仅表示软件 PWM 设定，不代表机械到位。
- 编译输出放 `build/`（Git 忽略），不提交临时程序、HEX 或日志。不删除已有文件。
- 使用现有 MinGW；只临时修改当前 PowerShell 进程 PATH，不安装依赖或修改系统配置。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$taskCompilerPath = $env:PATH
try {
    $env:PATH = 'D:/codeblocks/MinGW/bin;' + $taskCompilerPath
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_slave_bme280.c ../Bsp/bme280.c ../App/slave_bme280.c ../App/slave_protocol_runtime.c ../App/slave_master_status.c ../App/slave_web_control.c ../App/slave_servo_test.c ../Core/Src/i2c.c -o build/test_slave_bme280.exe
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

四路 PWM 寄存器测试编译真实驱动，使用专用 `stubs/tim.h`；沿用以上 PATH 与输出约定：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_servo_pwm.c ../Bsp/sg90_test_pwm.c -o build/test_servo_pwm.exe
& './build/test_servo_pwm.exe'
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

状态缓存：`gcc -std=c99 -Wall -Wextra -Werror -I../App test_slave_master_status.c ../App/slave_master_status.c -o build/test_slave_master_status.exe`，随后运行该程序。编辑网页后先执行 `node generate_slave_web_page.cjs`，再执行ESP/API测试生成真实JSON夹具及 `node check_slave_web_page.cjs`。

HC-SR04回归（在本目录执行，沿用上方MinGW PATH约定）：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../Bsp test_hcsr04_timer.c ../Bsp/hcsr04_timer.c -o build/test_hcsr04_timer.exe
& './build/test_hcsr04_timer.exe'
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../App -I../Bsp test_slave_hcsr04.c ../App/slave_hcsr04.c -o build/test_slave_hcsr04.exe
& './build/test_slave_hcsr04.exe'
```

网页控制服务：在tests执行 `gcc -std=c99 -Wall -Wextra -Werror -I../App test_slave_web_control.c ../App/slave_web_control.c ../App/slave_master_status.c -o build/test_slave_web_control.exe`，随后运行程序；HTML改动先生成C头，运行ESP测试产生真实gzip/JSON夹具，再运行Node网页回归。
# 5 cm范围验收（2026-10-06）

测距有效下限改为50 mm；test_slave_hcsr04.c补充49/50/51 mm有效性边界，网页回归同步检查5–50 cm范围；沿用本目录build产物和既有MinGW/Node工具，不安装依赖、不自动清理。
