# 从机电脑侧测试约定

- `test_*.c` 测试真实 BME 驱动、I2C 恢复流程、裸机采样与 LoRa 服务；`stubs/` 为电脑侧 HAL 替身，不能加入 Keil 工程。
- 测试结果只证明软件行为，不能代替实际 BME 应答、无线链路、上拉/供电验收。
- 编译输出放 `build/`（Git 忽略），不提交临时程序、HEX 或日志。不删除已有文件。
- 使用现有 MinGW；只临时修改当前 PowerShell 进程 PATH，不安装依赖或修改系统配置。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$taskCompilerPath = $env:PATH
try {
    $env:PATH = 'D:/codeblocks/MinGW/bin;' + $taskCompilerPath
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_slave_bme280.c ../Bsp/bme280.c ../App/slave_bme280.c ../App/slave_protocol_runtime.c ../Core/Src/i2c.c -o build/test_slave_bme280.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & './build/test_slave_bme280.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
} finally { $env:PATH = $taskCompilerPath }
```
