# roboparty CAN FD 双通道适配器使用手册

## 📖 简介

roboparty CAN FD 是一款基于 STM32G431 的单通道 CAN2.0 适配器，兼容开源的 `gs_usb` 协议（candleLight）。它支持 Windows 免驱使用（WinUSB）及 Linux 原生 SocketCAN 接口，适合开发调试及总线分析。

---

## 🔧 硬件规格

- **MCU**: STM32G431CBT6 (Cortex-M0+ @ 64MHz)
- **接口**:
  - 1x USB 2.0 Full Speed (Type-C)
  - 1x CAN2.0
- **指示灯**:
  - **蓝灯 (PC11)**: 系统状态指示
  - **绿灯 (PA1)**: 数据收发指示

---

## 🔨 固件编译与烧录

本项目基于 Zephyr RTOS 构建。

### 1. 编译环境配置

前提：确保已正确安装 [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)。

并配置 <https://github.com/CANnectivity/cannectivity>

之后克隆 <https://github.com/wentywenty/roboparty_canfd>

### 2. 编译

```bash
cd roboparty_canfd
west build -b roboparty_canfd
```

### 3. 烧录

本开发板配置了多种烧录器支持，请根据您使用的调试器选择命令：

- **OpenOCD (推荐 CMSIS-DAP/ST-Link)**:

  ```bash
  west flash --runner openocd
  ```

- **J-Link**:

  ```bash
  west flash --runner jlink
  ```

- **PyOCD**:

  ```bash
  west flash --runner pyocd
  ```

- **STM32CubeProgrammer (官方工具)**:

  ```bash
  west flash --runner stm32cubeprogrammer
  ```

- **Probe-rs**:

  ```bash
  west flash --runner probe-rs
  ```

---

## 💡 LED 状态说明

设备板载两个状态指示灯，分别指示系统状态与通信活动。

### 🔵 蓝灯 (PC11) - 系统状态

| 状态 | 闪烁模式 | 说明 |
|-----|---------|-----|
| **初始化** | 快闪 3 次 | 系统上电启动 |
| **空闲** | 慢闪 (0.1s 亮 / 1.9s 灭) | USB 已连接，CAN 通道关闭 |
| **就绪** | 呼吸闪 (0.5s 亮 / 0.5s 灭) | CAN 通道已打开 (Channel Started) |
| **错误** | 快速持续闪烁 | 系统或总线错误 |

### 🟢 绿灯 (PA1) - 通信指示

- **熄灭**: 总线空闲，无数据传输。
- **闪烁**: 检测到 CAN 总线数据接收 (RX) 或发送 (TX)。

---

## 🖥️ 上位机使用 (Windows)

Windows 下无需安装额外驱动，系统会自动识别为 WinUSB 设备。我们提供了 Python 编写的多设备管理上位机工具。

### 1. 运行工具

需要 Python 3.8+ 环境。

```bash
# 安装依赖
pip install pyusb
# 注意：Windows 用于通常自带 tkinter，Linux 可能需要 sudo apt install python3-tk

# 运行 (确保 libusb-1.0.dll 在目录下或系统路径中)
cd scripts
python roboparty_can_tool.py
```

### 2. 功能说明

- **多设备支持**: 专为 USB Hub 场景设计，支持同时连接并管理多个 CAN 适配器。底部列表实时显示设备总线地址及序列号。
- **全局连接**: 点击 **Connect All** 按钮，自动扫描并连接所有在线设备。
- **CAN 控制**:
  - 支持统一设置波特率（默认 1Mbps）。
  - 点击 **Start CAN** 可一键开启所有设备的 CAN 通道；点击 **Stop CAN** 一键关闭。
- **数据交互**:
  - **发送**: 支持向所有设备广播 (Target: All) 或向指定设备单发。支持 16 进制数据输入及周期性自动发送。
  - **接收**: 顶部日志区实时显示总线数据，自动标注数据来源设备编号 (`[Dev X]`)，并支持 ID 过滤。
- **打包**: 如需生成独立 EXE，可使用 PyInstaller 进行打包。

---

## 🐧 Linux 使用 (SocketCAN)

Linux 内核自带 `gs_usb` 驱动，即插即用。

### 1. 检查设备

```bash
dmesg | grep gs_usb
# 应显示: Configuring for 2 channels
```

### 2. 启动接口

```bash
# 设置波特率 1Mbps 并启动
sudo ip link set can0 up type can bitrate 1000000
sudo ip link set can1 up type can bitrate 1000000
```

### 3. 测试收发 (需安装 can-utils)

```bash
# 接收
candump can0

# 发送
cansend can0 123#DEADBEEF
```

---

## 🔍 常见问题排查

**Q1: Windows 无法识别设备？**

- 检查 USB 线缆是否支持数据传输。
- 检查设备管理器中是否有黄色感叹号，若有请手动更新驱动（选择 WinUSB）。

**Q2: Python 工具提示 "Device not found"？**

- 确认 `libusb-1.0.dll` 是否存在。
- Linux 下请检查 USB 权限 (`/etc/udev/rules.d/`)，确保当前用户有权访问 USB 设备。

**Q3: LED 持续快速闪烁？**

- 表示 CAN 总线错误。请检查：
  1. 终端电阻是否已连接（CAN总线两端各需 120Ω）。
  2. CAN_H / CAN_L 是否接反。
  3. 波特率是否匹配。

**Q4: 高波特率丢包？**

- 尝试改用更短、质量更好的 USB 线缆。
- 降低总线负载或发送频率。
