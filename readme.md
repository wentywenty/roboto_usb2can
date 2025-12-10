![alt text](image.png)
pyinstaller --onefile --windowed --name="RobopartyCAN" --add-binary="libusb-1.0.dll;." --icon=icon.ico scripts/roboparty_can_tool.py 

# roboparty CAN FD 双通道适配器使用手册

## 📋 目录
- [硬件规格](#硬件规格)
- [环境配置](#环境配置)
- [固件编译与烧录](#固件编译与烧录)
- [LED 状态指示](#led-状态指示)
- [Windows 上位机软件](#windows-上位机软件)
- [Linux 使用](#linux-使用)
- [故障排除](#故障排除)

---

## 🔧 硬件规格

### 主控芯片
- **MCU**: STM32G0B1cbT6
  - ARM Cortex-M0+ @ 64MHz
  - 512KB Flash / 144KB RAM
  - 2x FDCAN 控制器

### CAN 接口
- **通道**: 双路独立 CAN/CAN-FD
- **收发器**: ISO1050 (隔离型)
- **比特率**: 
  - 经典 CAN: 最高 1Mbps
  - CAN-FD: 数据阶段最高 8Mbps
- **隔离电压**: 2.5kV (DC-DC 隔离)

### USB 接口
- **协议**: USB 2.0 Full Speed
- **驱动**: WinUSB (Windows 免驱)
- **兼容**: gs_usb 协议 (Linux candleLight 兼容)

### 状态指示
- **LED_STAT (PC11)**: 系统状态 (5 种模式)
- **LED_WORD (PC12)**: 保留

---

## 🛠️ 环境配置

### 1. 安装 Zephyr SDK

**Windows (推荐使用 WSL2)**
```bash
# 更新系统
sudo apt update && sudo apt upgrade -y

# 安装依赖
sudo apt install -y git cmake ninja-build gperf ccache dfu-util \
  device-tree-compiler wget python3-dev python3-pip python3-setuptools \
  python3-tk python3-wheel xz-utils file make gcc gcc-multilib \
  g++-multilib libsdl2-dev libmagic1

# 安装 West
pip3 install --user west

# 初始化 Zephyr 工作空间
west init ~/zephyrproject
cd ~/zephyrproject
west update

# 安装 Python 依赖
pip3 install -r zephyr/scripts/requirements.txt

# 下载 Zephyr SDK (版本 0.16.x)
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.5/zephyr-sdk-0.16.5_linux-x86_64.tar.xz
tar xvf zephyr-sdk-0.16.5_linux-x86_64.tar.xz
cd zephyr-sdk-0.16.5
./setup.sh

# 设置环境变量
echo 'export ZEPHYR_BASE=~/zephyrproject/zephyr' >> ~/.bashrc
echo 'export PATH=$PATH:~/.local/bin' >> ~/.bashrc
source ~/.bashrc
```

### 2. 安装烧录工具

**OpenOCD (STM32 DFU/SWD)**
```bash
sudo apt install openocd

# 或者使用 STM32CubeProgrammer
# https://www.st.com/en/development-tools/stm32cubeprog.html
```

---

## 🔨 固件编译与烧录

### 1. 克隆项目
```bash
cd ~/zephyrproject/zephyr/samples
git clone <your-repo-url> roboparty_canfd
cd roboparty_canfd
```

### 2. 编译固件
```bash
# 设置 Zephyr 环境
cd ~/zephyrproject
source zephyr/zephyr-env.sh

# 编译 (使用自定义板级支持)
cd ~/zephyrproject/zephyr/samples/roboparty_canfd
west build -p -b roboparty_canfd

# 编译成功后固件位于: build/zephyr/zephyr.bin
```

### 3. 烧录固件

**方法 1: 使用 OpenOCD (推荐)**
```bash
# 连接 ST-Link 或 J-Link
west flash

# 或手动执行
openocd -f boards/roboparty_canfd/support/openocd.cfg \
  -c "program build/zephyr/zephyr.elf verify reset exit"
```

**方法 2: 使用 DFU (通过 USB)**
```bash
# 1. 按住 BOOT 按钮,插入 USB (进入 DFU 模式)
# 2. 检查 DFU 设备
lsusb | grep 0483:df11

# 3. 烧录
dfu-util -a 0 -s 0x08000000:leave -D build/zephyr/zephyr.bin
```

---

## 💡 LED 状态指示

### LED_STAT (PC11) 状态说明

| 状态 | 模式 | 说明 |
|-----|------|-----|
| **初始化** | 快闪 3 次 (100ms 周期) | 固件启动中 |
| **空闲** | 慢闪 (2 秒周期) | USB 未连接 |
| **USB 就绪** | 中速闪 (1 秒周期) | USB 已连接,等待通道启动 |
| **CAN 活动** | 短闪 (50ms) | CAN 收发数据时的脉冲指示 |
| **错误** | 快速闪 20 次 (4 秒) | 总线错误/离线,自动恢复 |

### 状态转换流程
```
上电 → 初始化 (3次快闪)
  ↓
空闲 (慢闪 2s) ← USB 断开
  ↓ USB 连接
USB 就绪 (中速闪 1s)
  ↓ 启动通道
CAN 活动 (50ms 短闪) ← 收发数据时
  ↓ 总线错误
错误 (快闪 20 次) → 自动恢复到 USB 就绪
```

### 错误恢复机制
- 错误状态闪烁 20 次(约 4 秒)后自动恢复
- 无需重启设备或重新连接 USB
- 保持通道启动状态,可立即继续通信

---

## 🖥️ Windows 上位机软件

### 1. 安装运行环境

**Python 环境 (开发/源码运行)**
```bash
# 安装 Python 3.8+
# 下载: https://www.python.org/downloads/

# 安装依赖
pip install pyusb tkinter

# 下载 libusb (必需)
# https://github.com/libusb/libusb/releases
# 解压 libusb-1.0.dll 到程序目录
```

### 2. 运行上位机软件

**方法 1: Python 源码运行**
```bash
cd scripts
python roboparty_can_tool.py
```

**方法 2: 使用打包的 EXE**
```bash
# 直接双击 RobopartyCAN.exe
# (已包含 libusb-1.0.dll,免安装)
```

### 3. 打包为独立 EXE

**使用 PyInstaller**
```bash
# 安装 PyInstaller
pip install pyinstaller

# 打包 (在项目根目录执行)
pyinstaller --onefile --windowed ^
    --name="RobopartyCAN" ^
    --add-binary="libusb-1.0.dll;." ^
    --icon=icon.ico ^
    scripts/roboparty_can_tool.py

# 生成的 EXE 位于: dist/RobopartyCAN.exe
```

### 4. 使用说明

#### 4.1 连接设备
1. 插入 USB 线缆,Windows 自动安装 WinUSB 驱动
2. 打开上位机软件
3. 点击 **"连接设备"** 按钮
4. 状态显示 "已连接"(绿色)

#### 4.2 启动 CAN 通道
1. 选择比特率 (默认 1Mbps)
2. 点击 **"启动通道"** 按钮
3. CAN0 和 CAN1 同时启动

#### 4.3 发送 CAN 帧
- **发送通道**: 选择 CAN0 或 CAN1
- **CAN ID**: 输入十六进制 ID (例如: `123`)
- **数据**: 输入十六进制字节,空格分隔 (例如: `AA BB CC DD`)
- 点击 **"发送"** 按钮

**周期发送**:
- 设置周期时间 (ms)
- 勾选 **"启用周期发送"**
- 取消勾选停止周期发送

#### 4.4 接收 CAN 帧
- 接收窗口实时显示所有帧
- **过滤 ID**: 输入十六进制 ID 只显示特定帧
- **清空**: 清除接收窗口内容

**接收显示格式**:
```
[时间戳] RX CH通道 ID:0xXXX DLC:长度 Data:数据
[10:56:06.530] RX CH1 ID:0x123 DLC:4 Data:AA BB CC DD
```

#### 4.5 双通道环回测试
```
1. 硬件连接: CAN0_H ↔ CAN1_H, CAN0_L ↔ CAN1_L
2. 启动通道
3. 选择 CAN0 发送
4. 观察接收窗口显示 CH1 收到帧
```

### 5. 界面截图说明

```
┌──────────────────────────────────────────────────┐
│ [连接设备] [断开连接]  ●已连接  比特率:[1000000▼] │
│ [启动通道] [停止通道]                              │
├──────────────────────────────────────────────────┤
│ 发送 CAN 帧                                       │
│ 发送通道: ⦿CAN0 ○CAN1                            │
│ CAN ID (hex): [123___]                           │
│ 数据 (hex):   [AA BB CC DD________________]      │
│ 周期发送(ms): [100_] □ 启用周期发送      [发送]  │
├──────────────────────────────────────────────────┤
│ 接收 CAN 帧              [清空] 过滤ID:[___] 0帧 │
│ ┌──────────────────────────────────────────────┐ │
│ │[10:56:06.530] TX CH0 ID:0x123 Data:AA BB CC│ │
│ │[10:56:06.581] RX CH1 ID:0x123 DLC:4 Data:AA│ │
│ │                                              │ │
│ └──────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
```

---

## 🐧 Linux 使用

### 1. 自动识别为 CAN 设备

**检查设备**
```bash
# 插入 USB 后检查
lsusb | grep 1d50:606f
# 输出: Bus 001 Device 010: ID 1d50:606f OpenMoko, Inc.

# 查看 gs_usb 驱动
dmesg | grep gs_usb
# 输出: gs_usb 1-1:1.0 can0: Configuring for 2 channels

# 查看 CAN 接口
ip link show | grep can
# 输出: 
#   can0: <NOARP,ECHO> mtu 16 qdisc noop state DOWN mode DEFAULT
#   can1: <NOARP,ECHO> mtu 16 qdisc noop state DOWN mode DEFAULT
```

### 2. 配置并启动 CAN

**设置比特率并启动**
```bash
# CAN0 配置为 1Mbps
sudo ip link set can0 up type can bitrate 1000000

# CAN1 配置为 1Mbps
sudo ip link set can1 up type can bitrate 1000000

# 查看状态
ip -details link show can0
```

### 3. 发送和接收

**使用 can-utils**
```bash
# 安装工具
sudo apt install can-utils

# 发送单帧
cansend can0 123#AABBCCDD

# 监听接收
candump can0

# 生成周期帧 (100ms)
cangen can0 -I 123 -L 4 -D AABBCCDD -g 100

# 环回测试
# 终端 1:
candump can1
# 终端 2:
cansend can0 123#11223344
```

### 4. 高级功能

**CAN-FD 模式 (需固件支持)**
```bash
sudo ip link set can0 up type can bitrate 1000000 dbitrate 8000000 fd on
```

**错误统计**
```bash
ip -s -d link show can0
```

**停止接口**
```bash
sudo ip link set can0 down
sudo ip link set can1 down
```

---

## 🔍 故障排除

### 问题 1: Windows 设备无法识别

**现象**: 设备管理器显示未知设备

**解决方案**:
```
1. 检查 USB 线缆是否支持数据传输
2. 更换 USB 端口 (优先使用 USB 2.0 端口)
3. 重新插拔设备
4. 检查固件是否正确烧录
```

### 问题 2: Python 上位机报错 "Device not found"

**现象**: `ValueError: Device not found (VID=0x1D50, PID=0x606F)`

**解决方案**:
```bash
# Windows: 检查设备管理器
# 1. 展开 "通用串行总线设备"
# 2. 查找 "gs_usb candleLight device"
# 3. 如果有黄色感叹号,右键更新驱动

# Linux: 检查权限
sudo chmod 666 /dev/bus/usb/00X/00Y
# 或添加 udev 规则
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="606f", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-roboparty-can.rules
sudo udevadm control --reload-rules
```

### 问题 3: Linux 下 0 pkts/s

**现象**: 接口 UP,但无数据传输

**检查步骤**:
```bash
# 1. 确认接口状态
ip link show can0
# 必须显示: UP,LOWER_UP

# 2. 检查比特率
ip -details link show can0

# 3. 测试环回
sudo ip link set can0 type can bitrate 1000000 loopback on
cansend can0 123#AABBCCDD
candump can0
# 应该能收到自己发的帧

# 4. 查看错误计数
ip -s link show can0
# RX-OVR, TX-ERR 应该为 0

# 5. 检查总线连接
# 确保 CAN_H/CAN_L 正确连接
# 确保有 120Ω 终端电阻
```

### 问题 4: LED 持续错误闪烁

**现象**: LED 快速闪烁 20 次后不恢复

**原因**:
- CAN 总线未连接终端电阻
- CAN_H/CAN_L 接反或短路
- 通道未正确启动

**解决方案**:
```
1. 检查硬件连接:
   - CAN_H ↔ CAN_H
   - CAN_L ↔ CAN_L
   - 两端各加 120Ω 终端电阻

2. 检查固件日志 (通过 UART 或 RTT)

3. 重新启动通道

4. 如果单设备测试,使用环回模式
```

### 问题 5: 高速通信丢包

**现象**: 1Mbps 下部分帧丢失

**解决方案**:
```
1. 检查 USB 线缆质量 (使用短线,≤1m)
2. 降低总线负载 (减少发送频率)
3. 检查内存占用 (固件 RAM 使用率)
4. 确认双端有终端电阻
5. 检查线缆长度:
   - 1Mbps: ≤40m
   - 500kbps: ≤100m
   - 125kbps: ≤500m
```

### 问题 6: Windows 图标不显示

**PyInstaller 打包后 EXE 无图标**

**解决方案**:
```bash
# 1. 确保 icon.ico 格式正确 (包含多尺寸)
# 使用在线工具: https://www.icoconverter.com/

# 2. 清理缓存重新打包
rmdir /s /q build dist
del RobopartyCAN.spec
pyinstaller --clean --onefile --windowed ^
    --icon=icon.ico ^
    --name="RobopartyCAN" ^
    --add-binary="libusb-1.0.dll;." ^
    scripts/roboparty_can_tool.py

# 3. 清除 Windows 图标缓存
ie4uinit.exe -show
taskkill /f /im explorer.exe
start explorer.exe
```

---

## 📞 技术支持

- **项目地址**: https://github.com/your-repo
- **问题反馈**: Issues
- **固件版本**: v1.0.0
- **协议兼容**: gs_usb / candleLight

---

## 📄 许可证

本项目基于 Apache License 2.0 开源。

---

## 🙏 致谢

- Zephyr RTOS
- gs_usb 协议
- candleLight 项目


这份文档涵盖了从环境配置、固件编译、LED 状态、Windows 上位机到 Linux 使用的完整流程,并包含详细的故障排除指南。保存为 `USER_MANUAL.md` 即可。这份文档涵盖了从环境配置、固件编译、LED 状态、Windows 上位机到 Linux 使用的完整流程,并包含详细的故障排除指南。保存为 `USER_MANUAL.md` 即可。