# roboto_usb2can 适配器使用手册

## 📖 简介

roboto_usb2can 是一款基于 STM32G431 的单通道 CAN2.0 固件，兼容开源的 `gs_usb` 协议（candleLight）。它支持 Windows 免驱使用（WinUSB）及 Linux 原生 SocketCAN 接口，适合开发调试及总线分析。

---

## 🔧 硬件规格

- **MCU**: STM32G431CBT6 (Cortex-M4+ @ 170MHz)
- **接口**:
  - 1x USB 2.0 Full Speed (Type-C)
  - 1x CAN2.0
- **指示灯**:
  - **蓝灯 (PC11)**: USB 系统状态指示
  - **黄灯 (PA7)**: CAN 系统状态指示
  - **绿灯 (PA1)**: 数据收发指示

---

## 🔨 固件编译与烧录

本项目基于 Zephyr RTOS 构建。

### 1. 编译环境配置

**前提**：确保已正确安装 [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)。

1. **配置 CANnectivity 模块**

   在文件 `zephyr/submanifests/cannectivity.yaml` 并写入以下内容：

   ```yaml
   manifest:
     projects:
       - name: cannectivity
         url: https://github.com/CANnectivity/cannectivity.git
         revision: main
         path: custom/cannectivity # adjust the path as needed
   ```

2. **更新工作区**

   ```bash
   west update
   ```

3. **获取项目源码**

   将本仓库克隆到 `zephyr/samples` 目录：

   ```bash
   git clone https://github.com/wentywenty/roboto_usb2can samples/roboto_usb2can
   ```

### 2. 编译

```bash
cd roboto_usb2can
west build -b roboto_usb2can
```

### 3. 烧录

本开发板配置了多种烧录器支持，请根据您使用的调试器选择命令：

- **STM32CubeProgrammer (推荐使用STLINK-V3MINIE)**:

  ```bash
  west flash --runner stm32cubeprogrammer
  ```

- **Probe-rs**:

  ```bash
  west flash --runner probe-rs
  ```

- **J-Link**:

  ```bash
  west flash --runner jlink
  ```

- **PyOCD**:

  ```bash
  west flash --runner pyocd
  ```

- **OpenOCD**:

  ```bash
  west flash --runner openocd
  ```

---

## 💡 LED 状态说明

设备板载三个状态指示灯，分别指示USB状态、CAN状态与通信活动。

### 🔵 蓝灯 - USB状态

| 状态 | 闪烁模式 | 说明 |
|-----|---------|-----|
| **就绪** | 中速闪 (0.5s 亮 / 0.5s 灭) | USB 连接正常，设备就绪 |
| **错误** | 快速闪烁 (0.1s 亮 / 0.1s 灭) | USB 通信错误 |

### 🟡 黄灯 - CAN状态

| 状态 | 闪烁模式 | 说明 |
|-----|---------|-----|
| **关闭** | 极慢闪 (0.05s 亮 / 3.95s 灭) | CAN 通道关闭或总线关闭 |
| **活跃** | 中速闪 (0.5s 亮 / 0.5s 灭) | CAN 通道已打开，正常工作 |
| **警告** | 慢闪 (0.2s 亮 / 1.8s 灭) | CAN 总线警告状态 |
| **错误** | 快速闪烁 (0.1s 亮 / 0.1s 灭) | CAN 总线错误或错误洪水 |

### 🟢 绿灯 - 通信指示

- **熄灭**: 总线空闲，无数据传输
- **短闪**: 检测到 CAN 总线数据接收 (RX) 或发送 (TX)

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
python roboto_usb2can_tool.py
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

### 3. 打包为 EXE (可选)

如果需要在没有 Python 环境的电脑上运行，可以打包为 EXE 文件。

1. **安装打包工具**:

   ```bash
   pip install pyinstaller
   ```

2. **执行打包**:

   ```bash
   cd scripts
   pyinstaller --noconfirm --onefile --windowed --clean --icon="icon.ico" --add-data "icon.ico;." roboto_usb2can_tool.py
   ```

   生成的文件位于 `scripts/dist/roboto_usb2can.exe`。

---

## 🐧 Linux 使用 (SocketCAN)

Linux 内核自带 `gs_usb` 驱动，即插即用。

### 1. 检查设备

```bash
dmesg | grep gs_usb
# 应显示: Configuring for 1 channels
```

### 2. 启动接口

```bash
# 设置波特率 1Mbps 并启动
sudo ip link set can0 up type can bitrate 1000000
```

### 3. 测试收发 (需安装 can-utils)

```bash
# 接收
candump can0

# 发送
cansend can0 123#DEADBEEF
```

### 4. 运行测试脚本

我们在 `scripts` 目录下提供了自动化测试脚本 `test_roboto_usb2can.sh`，用于快速验证 CAN 接口功能。

```bash
# 赋予执行权限
chmod +x scripts/test_roboto_usb2can.sh

# 运行测试
./scripts/test_roboto_usb2can.sh
```

---

### 5. 安装 udev 规则

请使用项目内的 [scripts/99-roboto-usb2can.rules](scripts/99-roboto-usb2can.rules) 文件。
更详细说明请参考 [udev-setup.md](udev-setup.md)。

#### 安装步骤

1. **复制规则文件**
   ```bash
   sudo cp scripts/99-roboto-usb2can.rules /etc/udev/rules.d/
   sudo chmod 644 /etc/udev/rules.d/99-roboto-usb2can.rules
   ```
2. **重新加载udev规则**
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```
3. **（可选）将当前用户加入plugdev和dialout组**
   ```bash
   sudo usermod -a -G plugdev,dialout $USER
   # 重新登录后生效
   ```

#### 功能说明
- 普通用户可直接访问设备，无需sudo
- 自动为设备创建 `/dev/roboto_usb2can*` 符号链接
- 支持SocketCAN自动生成can0接口
- 兼容libusb、gs_usb、ttyUSB/ttyACM等多种访问方式

#### 验证方法
1. 插入设备后，执行：
   ```bash
   lsusb | grep 1d50:606f
   dmesg | grep gs_usb
   ls -l /dev/roboto_usb2can*
   ip link show type can
   ```
2. 使用 `candump can0`、`cansend can0 123#DEADBEEF` 测试CAN通信

#### 故障排查
- 设备无权限：确认用户已加入plugdev组，或手动 `sudo chmod 666 /dev/roboto_usb2can*`
- 没有can0接口：检查内核gs_usb驱动是否加载，或查看dmesg日志
- 规则不生效：确认规则文件名以`99-`开头，且权限为644，重插设备或重启udev

#### 兼容性
- 适用于主流Linux发行版（Ubuntu, Debian, Fedora, Arch等）
- 需要内核自带gs_usb驱动（3.16及以上）
- 支持SocketCAN、libusb、tty等多种访问方式

#### 卸载方法
```bash
sudo rm /etc/udev/rules.d/99-roboto-usb2can.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## 🔍 常见问题排查

**Q1: Windows 无法识别设备？**

- 检查 USB 线缆是否支持数据传输。
- 检查设备管理器中是否有黄色感叹号，若有请手动更新驱动（使用 zadig 选择 WinUSB）。

**Q2: LED 持续快速闪烁？**

- 表示 CAN 总线错误。请检查：
  1. 终端电阻是否已连接（CAN总线两端各需 120Ω）。
  2. CAN_H / CAN_L 是否接反。
  3. 波特率是否匹配。

**Q3: 高波特率丢包？**

- 尝试改用更短、质量更好的 USB 线缆。
- 降低总线负载或发送频率。
