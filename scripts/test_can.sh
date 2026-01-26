#!/bin/bash

# =================配置区域=================
# 目标波特率
BITRATE=1000000      # 1Mbps
# 定义要测试的接口列表
INTERFACES=("can0" "can1" "can2" "can3")
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

    # 设置波特率
    ip link set $IF type can bitrate $BITRATE

    # 增加发送队列长度，防止 USB 拥堵时丢包
    ip link set $IF txqueuelen 2000

    ip link set $IF up
done

echo ">>> 启动 cangen 压力发生器..."

# --- 启动生成器 ---
for IF in "${INTERFACES[@]}"; do
    if [ -d "/sys/class/net/$IF" ]; then
        # 【核心修改 2】
        # -g 0: 无间隔全速发送
        # -I: ID 从接口号+100开始，区分不同源
        # 2>/dev/null: 屏蔽 "No buffer space" 报错，保持界面干净
        cangen $IF -g 0 -I "${IF: -1}00" -L 8 -D i -i 2>/dev/null &
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
    echo "      🚀 4通道 CAN 2.0 (Classic) 极速压力测试 - $(date +%T)"
    echo "========================================================================"
    printf "%-8s %-6s %-15s %-15s %-10s\n" "接口" "MTU" "TX 速率" "RX 速率" "总错误数"
    echo "------------------------------------------------------------------------"

    for IF in "${INTERFACES[@]}"; do
        if [ -d "/sys/class/net/$IF" ]; then
            # 读取新值
            rx_new=$(read_sys_val $IF "rx_packets")
            tx_new=$(read_sys_val $IF "tx_packets")
            errors=$(read_sys_val $IF "tx_errors")
            rx_errors=$(read_sys_val $IF "rx_errors")
            total_err=$((errors + rx_errors))

            # 获取当前 MTU 确认是否设置成功
            curr_mtu=$(cat /sys/class/net/$IF/mtu)

            # 计算速率
            tx_rate=$((tx_new - tx_old[$IF]))
            rx_rate=$((rx_new - rx_old[$IF]))

            # 颜色高亮逻辑
            tx_color="\033[32m" # Green
            if [ $tx_rate -eq 0 ]; then tx_color="\033[90m"; fi # Grey if 0

            rx_color="\033[36m" # Cyan
            if [ $rx_rate -eq 0 ]; then rx_color="\033[90m"; fi # Grey if 0

            err_color="\033[0m"
            if [ $total_err -gt 0 ]; then err_color="\033[31m"; fi # Red if error

            # 打印行
            printf "%-8s %-6s ${tx_color}%-15s\033[0m ${rx_color}%-15s\033[0m ${err_color}%-10s\033[0m\n" \
                "$IF" "$curr_mtu" "${tx_rate} pps" "${rx_rate} pps" "$total_err"

            # 更新旧值
            rx_old[$IF]=$rx_new
            tx_old[$IF]=$tx_new
        fi
    done

    echo "========================================================================"
    echo " [调优说明]: 已尝试将 MTU 设为 16 以优化 USB 传输效率。"
    echo " [操作提示]: 按 Ctrl+C 停止测试并关闭所有信号源。"
done