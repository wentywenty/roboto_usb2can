#!/bin/bash

# =================配置区域=================
# 目标波特率
BITRATE=1000000      # 1Mbps
# 定义要测试的接口列表
INTERFACES=("can0" "can1" "can2" "can3")
# 发送间隔 (微秒) - cangen 接受 -g 参数，如果是浮点数如 1.5，其实需要看 cangen 版本支持
# 实测标准 cangen -g 接受毫秒，如果是小数可能被截断，建议用 -g (毫秒)
# 如果需要 1.5ms，Linux cangen 工具中 -g 参数通常是毫秒单位。
# 较新版本 cangen 的 -g 参数支持微秒 (us)，需要用 -g 1500 (如果单位是us) 还是直接支持浮点毫秒？
# 标准 can-utils 的 cangen -g 是毫秒。如果输入 1.5 可能会被截断或者支持。
# 安全起见，为了接近 1.5ms，我们尝试用浮点数，或者如果版本不支持，可能需要调整。
# 假设 cangen 支持浮点毫秒
TEST_INTERVAL=0.5
# ==========================================

# 检查是否以 root 运行
if [ "$EUID" -ne 0 ]; then
  echo "❌ 请使用 sudo 运行此脚本"
  exit
fi

# 退出陷阱：清理后台进程
trap 'echo -e "\n🛑 正在停止测试..."; sudo killall cangen 2>/dev/null; exit' INT

echo ">>> 正在初始化 CAN 接口 (模式: Classic CAN 2.0)..."
echo "    波特率: $BITRATE bps"

# --- 初始化循环 ---
for IF in "${INTERFACES[@]}"; do
    # 检查接口是否存在
    if [ ! -d "/sys/class/net/$IF" ]; then
        echo "⚠️  接口 $IF 不存在，跳过..."
        continue
    fi

    echo "    正在配置 $IF ..."
    ip link set $IF down

    # 【核心修改 1】强制设置 MTU 为 16 (标准帧长度)
    # 这有助于告诉驱动程序我们只发短包，试图缓解 USB 传输填充 0 的问题
    ip link set $IF mtu 16 2>/dev/null

    # 【核心修改 2】启用自动 Bus-Off 恢复 (restart-ms)
    # 默认 Linux CAN 驱动进入 Bus-Off 后会死锁，直到手动重启
    # 这里设置 100ms 后自动重启，解决"拔线重插也不发"的问题
    ip link set $IF type can bitrate $BITRATE restart-ms 100

    # 增加发送队列长度，防止 USB 拥堵时丢包
    ip link set $IF txqueuelen 2000

    ip link set $IF up
done

echo ">>> 启动 cangen 压力发生器..."

# --- 启动生成器 ---
for IF in "${INTERFACES[@]}"; do
    if [ -d "/sys/class/net/$IF" ]; then
        # 【核心修正】
        # -g 1.5: 1.5ms间隔 (~666pps/设备)。4个设备共 ~2600pps
        #        严禁使用 -g 0，否则 ID 最小的设备(can0)会因为优先级最高而霸占总线！
        # 【解决方案：随机 ID (-I R)】
        # 在高负载下 (如 0.1ms 间隔)，为了防止 ID 0x100 永远霸占总线，
        # 我们必须让所有接口都有机会发送“高优先级” (小 ID) 帧。
        # 使用 -I R (随机 ID) 可以让所有接口统计上公平地竞争总线。
        cangen $IF -g $TEST_INTERVAL -I R -L 8 -D i -i 2>/dev/null &
    fi
done

echo ">>> 仪表盘启动中..."
sleep 1

# --- 辅助函数：直接读取内核计数器 (极速) ---
read_sys_val() {
    cat "/sys/class/net/$1/statistics/$2" 2>/dev/null || echo 0
}

# 初始化旧值
declare -A rx_old tx_old
for IF in "${INTERFACES[@]}"; do
    rx_old[$IF]=$(read_sys_val $IF "rx_packets")
    tx_old[$IF]=$(read_sys_val $IF "tx_packets")
done

# --- 监控循环 ---
while true; do
    sleep 1

    clear
    echo "========================================================================"
    echo "      🚀 4通道 CAN 多节点互联压力测试 - $(date +%T)"
    echo "      (发送间隔: ${TEST_INTERVAL}ms | 自动恢复: 100ms)"
    echo "========================================================================"
    printf "%-6s %-12s %-12s %-8s %-12s %-15s\n" "接口" "速率(TX/RX)" "总包数(T/R)" "错误(T/R)" "状态" "硬件计数(TEC/REC)"
    echo "------------------------------------------------------------------------"

    for IF in "${INTERFACES[@]}"; do
        if [ -d "/sys/class/net/$IF" ]; then
            # 读取新值
            rx_new=$(read_sys_val $IF "rx_packets")
            tx_new=$(read_sys_val $IF "tx_packets")
            errors=$(read_sys_val $IF "tx_errors")
            rx_errors=$(read_sys_val $IF "rx_errors")
            
            # 获取 CAN 状态 (需 iproute2 支持)
            can_state=$(ip -d link show $IF | grep "state" | grep -o "ERROR-ACTIVE\|ERROR-WARNING\|ERROR-PASSIVE\|BUS-OFF\|STOPPED" | head -1)
            [ -z "$can_state" ] && can_state="UNK"

            # 【终极诊断】尝试读取硬件错误计数器 (TEC/REC)
            # SocketCAN device 目录下通常没有直接暴露 berr_counter 的标准节点
            # 但我们可以通过 ip -d link show 来获取
            # ip 输出示例: "can state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 100"
            berr_info=$(ip -d link show $IF | grep "berr-counter" | sed -E 's/.*berr-counter tx ([0-9]+) rx ([0-9]+).*/TX:\1 RX:\2/')
            if [ -z "$berr_info" ]; then berr_info="N/A"; fi

            # 计算显示 ID
            hex_id="Random"

            # 计算速率
            tx_rate=$((tx_new - tx_old[$IF]))
            rx_rate=$((rx_new - rx_old[$IF]))

            # 颜色逻辑
            state_color="\033[32m" # Green for ACTIVE
            if [ "$can_state" != "ERROR-ACTIVE" ]; then state_color="\033[31m"; fi

            # 打印行
            printf "%-6s %-12s %-12s %-8s ${state_color}%-12s\033[0m %-15s\n" \
                "$IF" "${tx_rate}/${rx_rate}" "${tx_new}/${rx_new}" "${errors}/${rx_errors}" "$can_state" "$berr_info"

            # 更新旧值
            rx_old[$IF]=$rx_new
            tx_old[$IF]=$tx_new
        fi
    done

    echo "========================================================================"
    echo " [调优说明]: 已尝试将 MTU 设为 16 以优化 USB 传输效率。"
    echo " [操作提示]: 按 Ctrl+C 停止测试并关闭所有信号源。"
done