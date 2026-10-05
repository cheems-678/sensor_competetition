#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
仿真平台串口日志 → PC 上位机协议解析桥接工具
=============================================

用途
----
普利edu(puliedu) 是纯浏览器云端仿真平台, 板子的 USART 数据出不了浏览器沙箱,
无法直接接到本机的 pc_host.exe。本工具补上这段"最后一公里":

    puliedu 平台 UART 窗口 (hex 文本)
        → 复制保存成 .log
            → 本脚本还原成字节流
                → 用 pc_host.py 里【真实的 FrameParser】解析
                    → 打印人类可读结果 / 转发到真实串口给 pc_host 吃

因为复用的是 pc_host.py 里同一份解析器, 所以"仿真固件与实物固件协议一致"
这句话是有代码级证据的, 不是嘴上说。

用法
----
1) 解析一段 hex 文本
   python sim_bridge.py --hex "AA 55 13 00 01 10 00 FD 02 26 01 0F 02 6C 27 94 3C 02 01 07 3C 01 7E"

2) 解析平台导出的日志文件
   python sim_bridge.py --log sim_uart.log

3) 转发到串口, 让 pc_host.exe 真实接收 (需 com0com 之类虚拟串口对)
   python sim_bridge.py --log sim_uart.log --forward COM6 --loop --period 1.0

4) 生成一份示例日志, 先看效果
   python sim_bridge.py --make-sample

日志格式容忍度
--------------
以下写法都能认:
    AA 55 13 00 01 10 ...
    AA5513000110...
    0xAA 0x55 0x13 ...
    [10:23:45] AA 55 13 00 01 10 ...   <- 时间戳会被自动剔除
"""

import argparse
import io
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PC_HOST = os.path.join(os.path.dirname(HERE), "pc_host", "pc_host.py")

# ---------------------------------------------------------------- 协议加载

def load_protocol():
    """从 pc_host.py 抽出纯协议逻辑段(不含 tkinter/matplotlib), 保证与实物同源"""
    if not os.path.isfile(PC_HOST):
        raise SystemExit("找不到 pc_host.py: %s" % PC_HOST)
    text = io.open(PC_HOST, encoding="utf-8").read()
    try:
        i0 = text.index("HDR0, HDR1 = 0xAA, 0x55")
        i1 = text.index("# ============== 3D粮仓视图")
    except ValueError as e:
        raise SystemExit("pc_host.py 结构变了, 无法定位协议段: %s" % e)
    ns = {}
    exec(compile(text[i0:i1], "pc_host_logic", "exec"), ns)
    return ns


# ---------------------------------------------------------------- hex 还原

_TS = re.compile(r"\d{1,2}:\d{1,2}:\d{1,2}")


def hex_to_bytes(s):
    """把任意写法的十六进制文本还原成字节流"""
    s = _TS.sub(" ", s)                      # 去掉时间戳 10:23:45
    s = re.sub(r"\[[^\]]*\]", " ", s)        # 去掉 [xxx] 方括号内容
    s = re.sub(r"0[xX]", " ", s)             # 去掉 0x 前缀
    toks = re.findall(r"[0-9A-Fa-f]{2}", s)
    return bytes(int(t, 16) for t in toks)


# ---------------------------------------------------------------- 帧解析

def build_decoder(ns):
    i16, u16 = ns["i16"], ns["u16"]
    P = {k: v for k, v in ns.items() if k.startswith("PKT_")}
    NAME = {
        P["PKT_VMAP"]: "声速图", P["PKT_AMAP"]: "异常图", P["PKT_WAVE"]: "声发射波形",
        P["PKT_LEVEL"]: "料位", P["PKT_SUMMARY"]: "摘要", P["PKT_AE"]: "声发射报告",
        P["PKT_ENV"]: "环境", P["PKT_VENT"]: "两板对比", P["PKT_SOUND"]: "虫声(主板转发)",
        P["PKT_AUDIO"]: "包络(主板转发)", P["PKT_NODE_ENV"]: "仓内环境(从板)",
        P["PKT_NODE_SOUND"]: "虫声(从板)", P["PKT_NODE_AUDIO"]: "包络(从板)",
        P["PKT_ACK"]: "命令确认ACK",
    }
    # 通风原因 = 固件 vent_why: 0不需要 1内外温差 2内外湿差 3仓内超标 4手动
    REASON = {0: "无", 1: "温差", 2: "湿差", 3: "仓内超标", 4: "手动"}
    LEVEL = {0: "无", 1: "可疑", 2: "虫害", 3: "严重"}

    def n10(v, ok):
        """带有效位的数值: ok 为真且非哨兵值才渲染成数字, 否则显示 -- (判据同 pc_host.py)"""
        return "%.1f" % (v / 10.0) if (ok and v not in (0xFFFF, -32768)) else "--"

    def press10(x):
        """气压哨兵值 0 / 0xFFFF 表示"无此传感器", 不渲染成数字 (判据同 pc_host.py)"""
        return "%.1f hPa" % (x / 10.0) if x not in (0, 0xFFFF) else "--"

    def dec(t, seq, tot, d):
        tag = NAME.get(t, "0x%02X" % t)
        head = "[%s] seq=%d/%d len=%d" % (tag, seq, tot, len(d))
        try:
            if t == P["PKT_VENT"] and len(d) >= 16:
                fl = d[13]
                return "%s  舱外 %sC/%s%%  仓内 %sC/%s%%  气压 %s  通风强度 %d%%  原因=%s  虫害=%s  自动通风=%s" % (
                    head,
                    n10(i16(d[0], d[1]), fl & 1), n10(u16(d[2], d[3]), fl & 1),
                    n10(i16(d[4], d[5]), fl & 2), n10(u16(d[6], d[7]), fl & 2),
                    press10(u16(d[8], d[9])), d[10],
                    REASON.get(d[11], d[11]), LEVEL.get(d[12], d[12]),
                    "是" if d[15] else "否")
            if t == P["PKT_NODE_ENV"] and len(d) >= 10:
                fl = d[8]
                return "%s  仓内 %sC/%s%%  气压 %s" % (
                    head, n10(i16(d[0], d[1]), fl & 0x01), n10(u16(d[2], d[3]), fl & 0x01),
                    press10(u16(d[4], d[5])) if (fl & 0x04) else "--")
            if t in (P["PKT_SOUND"], P["PKT_NODE_SOUND"]) and len(d) >= 13:
                return "%s  ch0 RMS=%d 峰值=%d 主频=%dHz | ch1 RMS=%d 峰值=%d 主频=%dHz  等级=%s" % (
                    head, u16(d[0], d[1]), u16(d[2], d[3]), u16(d[4], d[5]),
                    u16(d[6], d[7]), u16(d[8], d[9]), u16(d[10], d[11]),
                    LEVEL.get(d[12], d[12]))
            if t == P["PKT_ENV"] and len(d) >= 8:
                return "%s  粉尘 %d  4路风扇 %d/%d/%d/%d%%  诱虫灯=%s" % (
                    head, u16(d[0], d[1]), d[2], d[3], d[4], d[5], "开" if d[6] else "关")
            if t == P["PKT_ACK"] and len(d) >= 2:
                st = {0: "执行成功", 1: "未知命令", 2: "已转发从板"}.get(d[1], d[1])
                return "%s  命令 0x%02X  状态=%s" % (head, d[0], st)
            if t in (P["PKT_VMAP"], P["PKT_AMAP"]):
                return "%s  网格分片 %d/%d (%d 字节)" % (head, seq, tot, len(d))
            if t == P["PKT_AUDIO"] or t == P["PKT_NODE_AUDIO"]:
                return "%s  包络 %d 点  首值=%d 峰值=%d" % (head, len(d), d[0], max(d) if d else 0)
            if t == P["PKT_WAVE"]:
                return "%s  波形分片 %d/%d (%d 字节)" % (head, seq, tot, len(d))
        except Exception as e:
            return "%s  (解析异常: %s)" % (head, e)
        return head + "  " + " ".join("%02X" % b for b in d[:24]) + ("..." if len(d) > 24 else "")

    return dec


def run(stream, ns, quiet=False):
    parser = ns["FrameParser"]
    dec = build_decoder(ns)
    out = []

    def on_frame(t, seq, tot, d):
        line = dec(t, seq, tot, d)
        out.append((t, seq, tot, d))
        if not quiet:
            print("  " + line)

    p = parser(on_frame)
    for b in stream:
        p.feed(b)
    return out


# ---------------------------------------------------------------- 示例

def sample_frames(ns):
    """造一组示例帧(与 test_protocol.py 同源), 用于 --make-sample / 自检"""
    HDR0, HDR1 = 0xAA, 0x55

    def pack(ptype, seq, total, data):
        cs = ptype ^ seq ^ total ^ len(data)
        for b in data:
            cs ^= b
        return bytes([HDR0, HDR1, ptype, seq, total, len(data)]) + bytes(data) + bytes([cs & 0xFF])

    t_out, rh_out = 253, 550
    t_in, rh_in = 271, 620
    press = 10132
    vent = [(t_out >> 8) & 0xFF, t_out & 0xFF, (rh_out >> 8) & 0xFF, rh_out & 0xFF,
            (t_in >> 8) & 0xFF, t_in & 0xFF, (rh_in >> 8) & 0xFF, rh_in & 0xFF,
            (press >> 8) & 0xFF, press & 0xFF, 60, 2, 1, 0x07, 60, 1]
    env = [320 >> 8, 320 & 0xFF, 40, 45, 50, 55, 1, 0]
    snd = []
    for v in (1234, 5678, 3200, 2345, 6789, 4100):
        snd += [(v >> 8) & 0xFF, v & 0xFF]
    snd += [2]
    node_env = []
    for v in (271, 620, 10132, 265):
        node_env += [(v >> 8) & 0xFF, v & 0xFF]
    node_env += [0x07, 1]

    fr = [pack(ns["PKT_VENT"], 0, 1, vent),
          pack(ns["PKT_ENV"], 0, 1, env),
          pack(ns["PKT_SOUND"], 0, 1, snd),
          pack(ns["PKT_NODE_ENV"], 0, 1, node_env),
          pack(ns["PKT_VMAP"], 0, 2, [(i * 3) & 0xFF for i in range(128)]),
          pack(ns["PKT_VMAP"], 1, 2, [((i + 128) * 3) & 0xFF for i in range(128)]),
          pack(ns["PKT_ACK"], 0, 1, [ns["CMD_VENT_MANUAL"], 0x00])]
    return fr


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="仿真平台串口日志 → PC 上位机协议桥")
    ap.add_argument("--hex", help="直接给一段十六进制文本")
    ap.add_argument("--log", help="平台 UART 窗口导出的日志文件")
    ap.add_argument("--make-sample", action="store_true", help="生成示例日志 sample_uart.log")
    ap.add_argument("--forward", help="转发到串口(如 COM6), 供 pc_host.exe 真实接收")
    ap.add_argument("--loop", action="store_true", help="循环发送(配合 --forward)")
    ap.add_argument("--period", type=float, default=1.0, help="循环周期秒, 默认 1.0")
    ap.add_argument("--quiet", action="store_true", help="不逐帧打印")
    args = ap.parse_args()

    ns = load_protocol()
    print("已从 pc_host.py 载入协议逻辑 (同源解析器)")

    if args.make_sample:
        fr = sample_frames(ns)
        p = os.path.join(HERE, "sample_uart.log")
        with io.open(p, "w", encoding="utf-8") as f:
            base = time.time()
            for i, b in enumerate(fr):
                f.write("[%s] %s\n" % (time.strftime("%H:%M:%S", time.localtime(base + i)),
                                       " ".join("%02X" % x for x in b)))
        print("已生成示例日志: %s (%d 帧)" % (p, len(fr)))
        print("  python sim_bridge.py --log sample_uart.log")
        return

    if args.hex:
        stream = hex_to_bytes(args.hex)
    elif args.log:
        stream = hex_to_bytes(io.open(args.log, encoding="utf-8", errors="ignore").read())
    else:
        print("未指定输入, 用内置示例帧演示:\n")
        stream = b"".join(sample_frames(ns))

    if not stream:
        raise SystemExit("没有解析到任何十六进制字节, 检查输入格式")

    print("还原字节流: %d 字节" % len(stream))
    print("-" * 78)
    frames = run(stream, ns, quiet=args.quiet)
    print("-" * 78)
    print("成功解析 %d 帧" % len(frames))

    if args.forward:
        try:
            import serial
        except ImportError:
            raise SystemExit("需要 pyserial: pip install pyserial")
        ser = serial.Serial(args.forward, 115200, timeout=1)
        print("已打开 %s @115200, 向 pc_host 转发 ...  (Ctrl+C 停止)" % args.forward)
        try:
            while True:
                ser.write(stream)
                time.sleep(args.period)
                if not args.loop:
                    break
        except KeyboardInterrupt:
            pass
        finally:
            ser.close()
        print("转发结束")


if __name__ == "__main__":
    main()
