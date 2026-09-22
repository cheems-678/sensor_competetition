# 上位机

当前可用的智能通风系统上位机统一放在此目录。

当前版本使用 LoRa 应用协议 v4：固定单组、18 字节单遥测帧、四路风机占空比控制。传感器未接入阶段，无效占位值统一显示为 `--`，数据写入新表 `telemetry_v4`，不会改动旧表。

## 目录约定

- `main.py`：唯一的上位机源码入口。
- `智能通风系统.exe`：由 `main.py` 打包生成的可执行文件。
- `tests/`：只存放上位机 Python 测试，不放 STM32 固件测试。

## 风扇控制约定

- 滑块显示整数百分比，松开鼠标或释放调节按键后发送；数字框可输入 0–100，再点“发送”。不在拖动过程中连续发送，避免占用 LoRa 链路。
- 每路显示待发送、等待确认、已确认或超时；仅收到匹配流水号的成功 ACK 才视为主机已执行。ACK 不代表风扇实际转速测量。
- 输入为空、非数字、非有限值或超出 0–100 时拒绝发送；未连接时不保存待发送指令，重连不会自动启动风扇。
- 风扇编号和 GPIO 标签必须与主机 `fan_pwm.c` 一致；本次硬件驱动接法确认前不远程启动风扇。
- 当前用户确认的通道 1/2/3/4 为 PA1/PB1/PB9/PB8，外接 MOS 管；占空比是 PWM 高电平比例，不是实测 RPM。主机启动默认全部 0%。

## 运行与验证

```powershell
python .\main.py
python -m unittest discover -s .\tests
```

打包时从本目录执行：

```powershell
python -m PyInstaller --noconfirm --onefile --windowed --name 智能通风系统 --distpath . --workpath .\build --specpath .\build main.py
```
