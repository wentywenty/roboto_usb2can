#!/usr/bin/env python3
"""
roboparty CAN FD 上位机工具
支持双通道 CAN 收发
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import usb.core
import usb.util
import threading
import time
import struct
from queue import Queue
from datetime import datetime

# gs_usb 协议常量
GS_USB_REQUEST_HOST_FORMAT = 0
GS_USB_REQUEST_BITTIMING = 1
GS_USB_REQUEST_MODE = 2
GS_USB_REQUEST_DEVICE_CONFIG = 9
GS_USB_REQUEST_BT_CONST = 5

GS_USB_CHANNEL_MODE_RESET = 0
GS_USB_CHANNEL_MODE_START = 1

# CAN 帧结构 (与固件中的 gs_host_frame 对应)
class CANFrame:
    def __init__(self):
        self.echo_id = 0xFFFFFFFF
        self.can_id = 0
        self.can_dlc = 0
        self.channel = 0
        self.flags = 0
        self.reserved = 0
        self.data = bytearray(64)
    
    def to_bytes(self):
        """打包为字节流"""
        data_to_send = self.data[:self.can_dlc]
        padding = 64 - len(data_to_send)
        packed = struct.pack('<IIBBBB',
                             self.echo_id,
                             self.can_id,
                             self.can_dlc,
                             self.channel,
                             self.flags,
                             self.reserved)
        return packed + data_to_send + bytes(padding)
    
    @staticmethod
    def from_bytes(data):
        """从字节流解包"""
        # ✅ 调试:打印原始数据
        print(f"[DEBUG] Received {len(data)} bytes: {data[:20].hex() if len(data) >= 20 else data.hex()}")
        
        # ✅ 修正:帧头实际需要 16 字节 (echo_id + can_id + dlc + ch + flags + reserved + 2字节对齐)
        if len(data) < 16:
            print(f"[ERROR] Frame too short: {len(data)} < 16")
            return None
        
        frame = CANFrame()
        
        try:
            # 帧头结构: 4+4+1+1+1+1 = 12 字节 + 4字节填充 = 16字节
            frame.echo_id, frame.can_id, frame.can_dlc, frame.channel, frame.flags, frame.reserved = \
                struct.unpack('<IIBBBB', data[:12])  # ← 改为 12 字节
            
            # 数据部分从第 16 字节开始 (12字节头 + 4字节填充)
            data_offset = 16
            if len(data) >= data_offset + frame.can_dlc:
                frame.data = bytearray(data[data_offset:data_offset+frame.can_dlc])
            else:
                print(f"[WARN] Data truncated: expected {frame.can_dlc} bytes, got {len(data)-data_offset}")
                frame.data = bytearray(data[data_offset:])
            
            print(f"[DEBUG] Parsed: echo_id=0x{frame.echo_id:08X}, id=0x{frame.can_id:03X}, "
                f"dlc={frame.can_dlc}, ch={frame.channel}")
            
            return frame
            
        except struct.error as e:
            print(f"[ERROR] Unpack failed: {e}")
            return None

# 比特率配置 (1Mbps 示例)
BITRATE_1M = {
    'prop_seg': 15,
    'phase_seg1': 15,
    'phase_seg2': 16,
    'sjw': 1,
    'brp': 4
}

class RobopartyCAN:
    def __init__(self):
        self.dev = None
        self.ep_in = None   # 改为 None，自动检测
        self.ep_out = None  # 改为 None，自动检测
        self.is_open = False
        self.rx_thread = None
        self.rx_running = False
        self.rx_callback = None
        
    def open(self, vid=0x1D50, pid=0x606F):
        """打开设备"""
        self.dev = usb.core.find(idVendor=vid, idProduct=pid)
        if self.dev is None:
            raise ValueError(f"Device not found (VID=0x{vid:04X}, PID=0x{pid:04X})")
        
        # Claim interface 0
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except:
            pass
        
        usb.util.claim_interface(self.dev, 0)
        
        # ✅ 自动检测端点地址
        cfg = self.dev.get_active_configuration()
        intf = cfg[(0, 0)]  # Interface 0, Alternate Setting 0
        
        # 查找 Bulk IN 和 Bulk OUT 端点
        ep_in = None
        ep_out_list = []
        
        for ep in intf:
            if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN:
                if usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK:
                    ep_in = ep.bEndpointAddress
                    print(f"Found Bulk IN endpoint: 0x{ep_in:02X}")
            else:
                if usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK:
                    ep_out_list.append(ep.bEndpointAddress)
                    print(f"Found Bulk OUT endpoint: 0x{ep.bEndpointAddress:02X}")
        
        if ep_in is None:
            raise ValueError("Bulk IN endpoint not found")
        
        if not ep_out_list:
            raise ValueError("Bulk OUT endpoint not found")
        
        self.ep_in = ep_in
        # 使用最后一个 OUT 端点（兼容 COMPATIBILITY_MODE）
        self.ep_out = ep_out_list[-1]
        
        print(f"Using endpoints: IN=0x{self.ep_in:02X}, OUT=0x{self.ep_out:02X}")
        
        self.is_open = True
        return True
        
    def close(self):
        """关闭设备"""
        self.stop_receive()
        if self.dev:
            usb.util.release_interface(self.dev, 0)
            usb.util.dispose_resources(self.dev)
        self.is_open = False
    
    def set_bitrate(self, channel, bitrate_config):
        """设置比特率"""
        data = struct.pack('<IIIII',
                           bitrate_config['prop_seg'],
                           bitrate_config['phase_seg1'],
                           bitrate_config['phase_seg2'],
                           bitrate_config['sjw'],
                           bitrate_config['brp'])
        
        self.dev.ctrl_transfer(
            0x41,  # bmRequestType: OUT, Vendor, Interface
            GS_USB_REQUEST_BITTIMING,
            channel,
            0,
            data
        )
        print(f"Channel {channel} bitrate configured")
    
    def start_channel(self, channel):
        """启动 CAN 通道"""
        data = struct.pack('<II', GS_USB_CHANNEL_MODE_START, 0)
        self.dev.ctrl_transfer(
            0x41,
            GS_USB_REQUEST_MODE,
            channel,
            0,
            data
        )
        print(f"Channel {channel} started")
    
    def stop_channel(self, channel):
        """停止 CAN 通道"""
        data = struct.pack('<II', GS_USB_CHANNEL_MODE_RESET, 0)
        self.dev.ctrl_transfer(
            0x41,
            GS_USB_REQUEST_MODE,
            channel,
            0,
            data
        )
        print(f"Channel {channel} stopped")
    
    def send_frame(self, channel, can_id, data):
        """发送 CAN 帧"""
        frame = CANFrame()
        frame.channel = channel
        frame.can_id = can_id
        frame.can_dlc = len(data)
        frame.data[:len(data)] = data
        
        self.dev.write(self.ep_out, frame.to_bytes(), timeout=1000)
    
    def receive_frame(self, timeout=100):
        """接收 CAN 帧"""
        try:
            data = self.dev.read(self.ep_in, 512, timeout=timeout)
            return CANFrame.from_bytes(bytes(data))
        except usb.core.USBError as e:
            if e.errno == 110:  # Timeout
                return None
            raise
    
    def start_receive(self, callback):
        """启动接收线程"""
        self.rx_callback = callback
        self.rx_running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()
    
    def stop_receive(self):
        """停止接收线程"""
        self.rx_running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=2)
    
    def _rx_loop(self):
        """接收循环"""
        while self.rx_running:
            try:
                frame = self.receive_frame(timeout=100)
                if frame and self.rx_callback:
                    self.rx_callback(frame)
            except usb.core.USBTimeoutError:
                # 超时是正常的,继续循环
                continue
            except Exception as e:
                print(f"RX error: {e}")
                break

# Tkinter GUI
class CANToolGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("roboparty CAN FD 上位机")
        self.root.geometry("900x700")
        
        self.can = RobopartyCAN()
        self.rx_queue = Queue()
        
        self._create_widgets()
        self._update_rx_display()
    
    def _create_widgets(self):
        """创建界面组件"""
        # 顶部控制栏
        control_frame = ttk.Frame(self.root, padding="5")
        control_frame.pack(fill=tk.X)
        
        ttk.Button(control_frame, text="连接设备", command=self.connect).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="断开连接", command=self.disconnect).pack(side=tk.LEFT, padx=5)
        
        self.status_label = ttk.Label(control_frame, text="未连接", foreground="red")
        self.status_label.pack(side=tk.LEFT, padx=20)
        
        # 通道选择和比特率
        ttk.Label(control_frame, text="通道:").pack(side=tk.LEFT, padx=5)
        self.channel_var = tk.IntVar(value=0)
        ttk.Radiobutton(control_frame, text="CAN0", variable=self.channel_var, value=0).pack(side=tk.LEFT)
        ttk.Radiobutton(control_frame, text="CAN1", variable=self.channel_var, value=1).pack(side=tk.LEFT)
        
        ttk.Label(control_frame, text="比特率:").pack(side=tk.LEFT, padx=10)
        self.bitrate_var = tk.StringVar(value="1000000")
        ttk.Combobox(control_frame, textvariable=self.bitrate_var, width=10,
                     values=["125000", "250000", "500000", "1000000"]).pack(side=tk.LEFT)
        
        ttk.Button(control_frame, text="启动", command=self.start_channel).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="停止", command=self.stop_channel).pack(side=tk.LEFT)
        
        # ✅ 修改按钮布局
        ttk.Button(control_frame, text="启动全部", command=self.start_all_channels, 
                style="Accent.TButton").pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="停止全部", command=self.stop_all_channels).pack(side=tk.LEFT)
        
        # 发送区域
        send_frame = ttk.LabelFrame(self.root, text="发送 CAN 帧", padding="10")
        send_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(send_frame, text="CAN ID (hex):").grid(row=0, column=0, sticky=tk.W, pady=5)
        self.send_id_var = tk.StringVar(value="123")
        ttk.Entry(send_frame, textvariable=self.send_id_var, width=15).grid(row=0, column=1, sticky=tk.W)
        
        ttk.Label(send_frame, text="数据 (hex):").grid(row=1, column=0, sticky=tk.W, pady=5)
        self.send_data_var = tk.StringVar(value="AA BB CC DD")
        ttk.Entry(send_frame, textvariable=self.send_data_var, width=40).grid(row=1, column=1, sticky=tk.W)
        
        ttk.Button(send_frame, text="发送", command=self.send_frame).grid(row=0, column=2, rowspan=2, padx=10)
        
        ttk.Label(send_frame, text="周期发送 (ms):").grid(row=2, column=0, sticky=tk.W, pady=5)
        self.period_var = tk.StringVar(value="100")
        ttk.Entry(send_frame, textvariable=self.period_var, width=10).grid(row=2, column=1, sticky=tk.W)
        
        self.periodic_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(send_frame, text="启用周期发送", variable=self.periodic_var,
                        command=self.toggle_periodic).grid(row=2, column=2, sticky=tk.W)
        
        # 接收区域
        recv_frame = ttk.LabelFrame(self.root, text="接收 CAN 帧", padding="10")
        recv_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # 工具栏
        recv_toolbar = ttk.Frame(recv_frame)
        recv_toolbar.pack(fill=tk.X)
        
        ttk.Button(recv_toolbar, text="清空", command=self.clear_recv).pack(side=tk.LEFT, padx=5)
        
        self.filter_var = tk.StringVar()
        ttk.Label(recv_toolbar, text="过滤 ID:").pack(side=tk.LEFT, padx=5)
        ttk.Entry(recv_toolbar, textvariable=self.filter_var, width=10).pack(side=tk.LEFT)
        
        self.rx_count_label = ttk.Label(recv_toolbar, text="接收: 0 帧")
        self.rx_count_label.pack(side=tk.RIGHT, padx=10)
        
        # 接收文本框
        self.recv_text = scrolledtext.ScrolledText(recv_frame, height=20, font=("Consolas", 9))
        self.recv_text.pack(fill=tk.BOTH, expand=True, pady=5)
        
        self.rx_count = 0
        self.periodic_thread = None
        self.periodic_running = False
    
    def connect(self):
        """连接设备"""
        try:
            self.can.open()
            self.status_label.config(text="已连接", foreground="green")
            self.can.start_receive(self.on_frame_received)
            messagebox.showinfo("成功", "设备连接成功！")
        except Exception as e:
            messagebox.showerror("错误", f"连接失败: {e}")
    
    def disconnect(self):
        """断开连接"""
        self.stop_periodic()
        self.can.close()
        self.status_label.config(text="未连接", foreground="red")
    
    def start_channel(self):
        """启动 CAN 通道"""
        if not self.can.is_open:
            messagebox.showwarning("警告", "请先连接设备")
            return
        
        try:
            channel = self.channel_var.get()
            self.can.set_bitrate(channel, BITRATE_1M)
            self.can.start_channel(channel)
            messagebox.showinfo("成功", f"通道 {channel} 已启动")
        except Exception as e:
            messagebox.showerror("错误", f"启动失败: {e}")
    
    def stop_channel(self):
        """停止 CAN 通道"""
        if not self.can.is_open:
            return
        
        try:
            channel = self.channel_var.get()
            self.can.stop_channel(channel)
            messagebox.showinfo("成功", f"通道 {channel} 已停止")
        except Exception as e:
            messagebox.showerror("错误", f"停止失败: {e}")
    
    def send_frame(self):
        """发送 CAN 帧"""
        if not self.can.is_open:
            messagebox.showwarning("警告", "请先连接设备")
            return
        
        try:
            channel = self.channel_var.get()
            can_id = int(self.send_id_var.get(), 16)
            data_str = self.send_data_var.get().replace(" ", "")
            data = bytes.fromhex(data_str)
            
            self.can.send_frame(channel, can_id, data)
            
            # 显示在接收框
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            msg = f"[{timestamp}] TX CH{channel} ID:0x{can_id:03X} Data:{data.hex(' ').upper()}\n"
            self.recv_text.insert(tk.END, msg, "tx")
            self.recv_text.tag_config("tx", foreground="blue")
            self.recv_text.see(tk.END)
            
        except ValueError as e:
            messagebox.showerror("错误", f"数据格式错误: {e}")
        except Exception as e:
            messagebox.showerror("错误", f"发送失败: {e}")
    
    def toggle_periodic(self):
        """切换周期发送"""
        if self.periodic_var.get():
            self.start_periodic()
        else:
            self.stop_periodic()
    
    def start_periodic(self):
        """启动周期发送"""
        self.periodic_running = True
        self.periodic_thread = threading.Thread(target=self._periodic_loop, daemon=True)
        self.periodic_thread.start()
    
    def stop_periodic(self):
        """停止周期发送"""
        self.periodic_running = False
        if self.periodic_thread:
            self.periodic_thread.join(timeout=1)
    
    def _periodic_loop(self):
        """周期发送循环"""
        while self.periodic_running:
            try:
                self.send_frame()
                period_ms = int(self.period_var.get())
                time.sleep(period_ms / 1000.0)
            except:
                break
    
    def on_frame_received(self, frame):
        """接收回调"""
        self.rx_queue.put(frame)
    
    def _update_rx_display(self):
        """更新接收显示"""
        while not self.rx_queue.empty():
            frame = self.rx_queue.get()
            
            # 过滤空帧
            if frame.can_id == 0 and frame.can_dlc == 0:
                print(f"[SKIP] Empty frame on CH{frame.channel}")
                continue
            
            # 过滤 ID
            filter_id = self.filter_var.get().strip()
            if filter_id:
                try:
                    if int(filter_id, 16) != frame.can_id:
                        continue
                except:
                    pass
            
            self.rx_count += 1
            self.rx_count_label.config(text=f"接收: {self.rx_count} 帧")
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            # ✅ 修复: 只显示前 dlc 字节
            data_hex = frame.data[:frame.can_dlc].hex(' ').upper()
            msg = f"[{timestamp}] RX CH{frame.channel} ID:0x{frame.can_id:03X} DLC:{frame.can_dlc} Data:{data_hex}\n"
            
            self.recv_text.insert(tk.END, msg, "rx")
            self.recv_text.tag_config("rx", foreground="green")
            self.recv_text.see(tk.END)
            
            if int(self.recv_text.index('end-1c').split('.')[0]) > 1000:
                self.recv_text.delete('1.0', '100.0')
        
        self.root.after(50, self._update_rx_display)
        
    def clear_recv(self):
        """清空接收"""
        self.recv_text.delete('1.0', tk.END)
        self.rx_count = 0
        self.rx_count_label.config(text="接收: 0 帧")

    def start_all_channels(self):
        """启动所有 CAN 通道"""
        if not self.can.is_open:
            messagebox.showwarning("警告", "请先连接设备")
            return
        
        try:
            # 启动 CAN0 和 CAN1
            for ch in [0, 1]:
                self.can.set_bitrate(ch, BITRATE_1M)
                self.can.start_channel(ch)
                print(f"Channel {ch} started")
            
            messagebox.showinfo("成功", "CAN0 和 CAN1 已启动")
        except Exception as e:
            messagebox.showerror("错误", f"启动失败: {e}")

    def stop_all_channels(self):
        """停止所有 CAN 通道"""
        if not self.can.is_open:
            return
        
        try:
            for ch in [0, 1]:
                self.can.stop_channel(ch)
                print(f"Channel {ch} stopped")
            
            messagebox.showinfo("成功", "CAN0 和 CAN1 已停止")
        except Exception as e:
            messagebox.showerror("错误", f"停止失败: {e}")
            
def main():
    root = tk.Tk()
    app = CANToolGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()