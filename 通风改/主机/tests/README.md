# 主机电脑侧回归测试

- 本目录仅放 `test_*.c` 和测试说明；`stubs/` 仅放测试 HAL 替身，不能加入 Keil 固件工程或固件 include path。
- 测试编译真实 `App/master_bme280.c`、`App/master_runtime.c`，替换传感器寄存器访问、灯带、风扇和队列接口，验证状态机与通信行为，不宣称验证真实硬件。
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
