# 控制室电脑侧协议回归

## 单只 SG90 窗口转发约定

- 以仓库 `PROTOCOL_V4.md` 为准，新增窗口类型11及载荷 `[1,0/1]` 的PC白名单，控制室验证后原样发给主机；自动轮询保持关闭，射频参数不改。
- ACK仍为两字节 `[原命令,status]`，窗口等待沿用6秒；只匹配主机来源、组号、flow和原命令11才能结束事务。上位机可收到主机结果或控制室超时ERROR；控制室不得生成窗口成功ACK，也不直接接受从机ACK。
- 测试新增开/关原样转发、非法编号/动作/长度/CRC拒收、窗口ACK及错flow/命令、排队、超时与回绕；原遥测和风机回归不变。产物仅在既有build目录，固件由用户烧录。

- 本目录仅放 `test_*.c` 和说明；HAL替身若需要则仅放 `stubs/`。测试不加入Keil。
- `test_gateway_runtime.c` 编译真实 `Bsp/gateway_runtime.c`，通过虚拟串口字节和发送回调测试PC查询、18/26字节遥测原样转发、长度/标志/CRC拒收、分包粘包、错地址/flow不能结束等待、风机ACK以及超时回绕；不声称验证硬件或射频。
- 产物仅放已忽略的 `build/`。不要提交可执行文件或为本测试改系统PATH、射频参数及自动轮询策略。
- 在本目录运行PowerShell：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$taskCompilerPath = $env:PATH
try {
    $env:PATH = 'D:/codeblocks/MinGW/bin;' + $taskCompilerPath
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../Bsp test_gateway_runtime.c ../Bsp/gateway_runtime.c -o build/test_gateway_runtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & './build/test_gateway_runtime.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
} finally {
    $env:PATH = $taskCompilerPath
}
```

Keil构建和用户手动烧录后，再验收控制室到主机双向通信、39字节完整转发及至少五分钟并行运行。电脑测试不能替代实际UART/LoRa验收。

## 三板窗口控制整链路模拟

- `test_window_chain.c` 组合真实控制室gateway、主机协议/runtime/ingress/queues、从机协议runtime及舵机手动状态机；仅替换HAL tick、BME/声学数据、灯带/风机/PWM和无线字节transport，不接硬件、不发真实命令。
- 验证控制室→主机→从机的CRC/地址/流水号关联，开1700 us/关1300 us运行300 ms后恢复1500 us停止脉宽、同方向在途重复不续时/反方向重新计时、业务启动不自动往返，窗口前后遥测字段隔离，以及从机掉线、ACK丢失仍按时停止但结果未知、驱动失败、1000 ms排队取消和tick回绕。ACK仍只确认动作PWM设置，不确认自动停止或机械到位。
- 从机runtime单独使用从机HAL桩编译，避免混用两板的HAL/lora头；其余模块沿用主机HAL桩。此测试模拟字节transport和实际发送通知，不编译真实无线驱动；半双工发送重试仍由主机`test_master_lora_tx.c`单独验证。
- 在本目录、使用上文临时MinGW PATH执行；产物仍仅放既有`build/`，不加入Keil、不替代实板动作及机械验收。

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../../../从机/tests/stubs -I../../../从机/App -I../../../从机/Bsp -c ../../../从机/App/slave_protocol_runtime.c -o build/chain_slave_runtime.o
if ($LASTEXITCODE -ne 0) { throw 'Slave runtime compilation failed' }
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../../../主机/tests/stubs -I../../../主机/App -I../../../主机/Bsp -I../../../从机/App -I../../../从机/Bsp -I../Bsp test_window_chain.c ../Bsp/gateway_runtime.c ../../../主机/App/lora_protocol.c ../../../主机/App/master_runtime.c ../../../主机/App/master_ingress.c ../../../主机/App/master_queues.c ../../../从机/App/slave_servo_test.c build/chain_slave_runtime.o -o build/test_window_chain.exe
if ($LASTEXITCODE -ne 0) { throw 'Chain compilation failed' }
& './build/test_window_chain.exe'
if ($LASTEXITCODE -ne 0) { throw 'Chain test failed' }
```
