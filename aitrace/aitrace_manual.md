# AITrace — AI 驱动 MCU 调试 CLI 工具

AITrace 是独立的命令行调试工具，通过 OpenOCD 提供的 TCP 通道读取 MCU调试数据，输出纯文本供人工或 AI 分析。无需 GUI。

与 mstudio（GUI）共享同一套代码库与 `libmstudiocore.a`：mstudio 负责可视化交互，aitrace 是同一套调试能力的机器接口。

## 快速开始

```powershell
# 1. 启动 OpenOCD + RTT（在 modus_template 根目录下）
.\make.bat rtt          # 已固化:SWD 8MHz + rtt polling_interval 10

# 2. 运行 aitrace
./tools/aitrace                  # 无参数打印全部用法
./tools/aitrace shell help       # 列出固件 shell 命令
./tools/aitrace wave stat 5      # 波形链路质量统计
./tools/aitrace wave capture 5 --output w.csv
```

## 架构

```
aitrace.exe
├── OpenOCD Telnet  :4444  →  ocd halt|resume|regs|peek|mdw|stack|raw
├── RTT Ch0         :9090  →  shell <命令> | wave start|stop|rate|list
├── RTT Ch1         :9091  →  wave capture <秒数> | wave stat [秒数]
├── ELF/MAP 文件    (磁盘) →  map resolve|info | crash report
└── GDB             :3333  →  gdb connect|break|step|print|bt|detach
```

## 侵入等级

| 等级 | 方式 | CPU 影响 | 需确认 |
|------|------|----------|--------|
| **A. 被动** | `shell ...`, `wave ...`, `serial ...` | 无 | 否 |
| **B. 暂停** | `ocd regs/peek/mdw/stack` | 暂停约 1 秒 | **是** |
| **C. GDB** | `gdb connect/break/step` | 完全调试控制 | **是** |

**始终优先使用被动命令。** MCU 可能正在驱动电机或功率级电路。

## 命令参考

### 被动命令 — 不打断 CPU

#### shell — 通过 RTT Ch0 发送命令 (TCP 9090)

```powershell
aitrace shell --raw <cmd>             # 原始流输出（不过滤固件日志）
aitrace shell <cmd...>                # 发送命令，默认过滤 [T] 级日志行
aitrace shell help                    # 列出固件所有 shell 命令
aitrace shell regs                    # 固件端寄存器转储
aitrace shell peek <hex_addr>         # 被动读取内存（uint32）
aitrace shell stack [n]               # 被动栈转储
aitrace shell cfsr                    # 查看故障状态寄存器
aitrace shell list                    # 列出 MODUS 对象
aitrace shell wave drop               # 波形 FIFO 丢帧统计
aitrace shell log -E -W -I -D         # 切换日志级别
aitrace shell ver                     # 查看 MODUS 版本
```

行为说明（V0.5.0.4 重构）：

- **日志过滤**：固件的 TRACE 级日志（如周期心跳 `[T] [Heartbeat] ...`）默认被
  过滤，只保留命令响应（`[I]/[W]/[E]` 级）。`--raw` 关闭过滤，输出原始字节流。
- **RTT 积压排泄**：发送命令前先用 400ms 窗口排泄 RTT Ch0 积压数据（启动
  横幅、历史日志等），避免陈旧 prompt 残留造成提前终止。
- **自动终止**：收到 shell 提示符 `\n> ` 即返回；日志持续刷屏也不会挂起
  （最后收到数据后 600ms 空闲超时 + 5 秒硬上限）。

#### wave — 波形采集与链路统计 (TCP 9091)

```powershell
aitrace wave list                  # 列出当前波形通道
aitrace wave start                 # 开始采集
aitrace wave stop                  # 停止采集
aitrace wave rate 10               # 设置抽取率（1=最快，0=外部驱动）
aitrace wave capture 5             # 捕获 5 秒输出到 stdout
aitrace wave capture 10 --output w.csv  # 捕获到文件
aitrace wave stat 5                # 链路质量统计（最常用）
```

**CSV 格式**：`time,通道名1,通道名2,...`。注意：

- `time` 是**主机接收时间戳**（秒，double，单调时钟），不是 µs；
- 通道值是**经 scale 换算后的 float**（如 IqRef scale=1000，原始
  int16 1000 显示为 1.0），未推送的通道列为空；
- 表头来自描述帧（每秒重发一次），捕获开始后最多 1 秒才有表头。

**wave stat — 链路质量三指标**：

```
rate 999.924 f/s   crc_err 0   seq_lost 0     ← 每秒一行
summary: 3032 frames (50.001 KB total), crc_err 0, seq_lost 0
```

- `rate`：每秒正确解析的数据帧数，1ms 推送时应 ≈1000；
- `crc_err`：CRC 校验失败次数，>0 说明字节流损坏/解析错位；
- `seq_lost`：序号缺口（整帧在设备→主机途中丢失），>0 说明链路丢帧。

验收标准：**rate≈推送率、crc_err=0、seq_lost=0**。
与固件侧 `aitrace shell wave drop` 交叉验证，账目闭合关系：
`产帧数(Total增量) = ok + seq_lost + 设备FIFO丢帧(Drop增量)`。
（stat 启动时会先静默解析至首个描述帧以学习掩码长度，重同步杂波
不会污染统计。）

### 暂停命令 — 短暂暂停 CPU（每次命令自动 暂停→读取→恢复）

```powershell
aitrace ocd regs                   # 全部核心寄存器（R0-R12, SP, LR, PC, xPSR）
aitrace ocd peek 0x20000000        # 读取 SRAM 基地址 uint32
aitrace ocd mdw 0x20000000 16      # 从 SRAM 转储 16 个字
aitrace ocd stack 32               # 转储 SP 周围 32 个字
aitrace ocd halt                   # 仅暂停 CPU（保持暂停状态）
aitrace ocd resume                 # 恢复 CPU
aitrace ocd raw "rtt status"       # 透传任意 OpenOCD 命令
```

### GDB — 完整调试控制（需显式确认）

```powershell
aitrace gdb connect --elf build/template.elf
aitrace gdb break main.c:100
aitrace gdb continue / step / print <变量> / bt / detach
```

### 分析命令 — 符号解析与崩溃报告

```powershell
# 地址解析为符号
aitrace map resolve build/template.elf 0x08004a82 0x08004bff
# 内存段概览
aitrace map info build/template.map
# 崩溃分析（从 RTT Ch0 的异常打印中提取 PC/LR/SP）
aitrace crash report --pc=<PC> --lr=<LR> --sp=<SP> \
    --elf=build/template.elf --cfsr=0x00000100
```

CFSR 位域解码（`crash report` 内置）：

| CFSR 位 | 异常类型 |
|---------|----------|
| UFSR[0] | 未定义指令 |
| UFSR[2] | 无效 PC 加载 |
| UFSR[5] | 除零错误 |
| UFSR[6] | 非对齐访问 |
| BFSR[1] | 精确数据总线错误 |
| BFSR[3]/[4] | 出栈/入栈总线错误 |

## 诊断工作流

### HardFault 分析

```
1. 观察 RTT Ch0 输出 —— 固件异常时自动打印异常帧 + CFSR
2. 提取 PC、LR、SP
3. aitrace crash report --pc=<PC> --lr=<LR> --sp=<SP> --elf=build/template.elf
```

### 运行时行为分析

```
1. aitrace wave stat 5              # 先确认链路质量（rate/crc/seq_lost）
2. aitrace wave capture 5 > wave.csv
3. aitrace shell wave drop          # 设备侧丢帧计数（被动）
4. 分析 CSV：查找异常值，与预期范围对比
```

## RTT 链路行为须知（实测结论）

- **无客户端 = 不传输**：9091 没有任何 TCP 客户端时，OpenOCD 不抽取RTT 数据，设备侧 FIFO 会把所有帧丢弃（`wave drop` 里 Drop 暴涨是设计行为，不是故障）。测量前必须先挂上客户端。
- **多客户端 OK**：9091 可挂多个 TCP 客户端，每个都收到完整数据（mstudio 和 aitrace 可同时看波形）。
- **吞吐天花板**：OpenOCD 每次轮询最多读 1024 字节。默认 100ms 轮询⇒ ~10 KB/s 上限；`make.bat rtt` 已固化 `rtt polling_interval 10`
  + SWD 8MHz ⇒ ~100 KB/s，实测 16 通道 @1kHz（38 KB/s）零丢帧。
- **协议保留值**：数据帧序号永不取 0xFD（描述帧类型），解析器按`AA 55` 同步、从描述帧学习掩码字节数即可正确解析任意通道数。

## 环境要求

- **OpenOCD** 已启用 RTT（端口 4444、9090、9091）
- **ELF 文件** 用于符号解析（如 `build/template.elf`）
- **gdb-multiarch** 在 PATH 中（GDB 命令需要）
- **Windows**: MSYS2/MinGW-w64 运行时

## 构建与打包

```bash
# 在 E:\Project\mstudio\aitrace 目录下
make clean-all && make

# 产物：aitrace.exe（链接 libmstudiocore.a + ws2_32）
# 打包工具包 = aitrace.exe + 本 README
# 部署：拷贝到 modus_template\tools\
cp aitrace.exe E:\Project\modus_template\tools\
cp README.md     E:\Project\modus_template\tools\aitrace_manual.md
```

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 用法错误或连接失败 |
| 3 | 工具内部异常（请报告 bug） |

## 常见问题

### 连接失败 / 端口无响应

```powershell
# 检查 OpenOCD 是否在运行
tasklist | findstr openocd

# 全部终止后重新启动（两个 OpenOCD 争抢同一探针会导致 CMSIS-DAP 错误）
taskkill /F /IM openocd.exe
.\make.bat rtt
```

### CMSIS-DAP 错误 / 日志刷 `error writing data`

调试探针 USB 断连后 OpenOCD 不会自愈，端口虽然在监听但读不到数据。按上面步骤杀掉重启即可。

### shell 响应被日志淹没

旧版问题，已内置解决：默认过滤 `[T]` 级日志行、识别提示符自动返回。仍想看不过滤的原始流时加 `--raw`；或先用 `aitrace shell log -T` 关闭固件 TRACE 日志，也可用 `aitrace ocd regs`（走 telnet，不受 RTT 日志影响）。

## 安全守则

1. **被动优先**：始终先尝试 `shell` 和 `wave` 命令
2. **暂停/GDB 需确认**：CPU 暂停会中断实时控制环路
3. **未经工程师确认，绝不修改固件或重新烧录**
4. **检查重复 OpenOCD 实例**：两个 OpenOCD 争抢调试探针会导致CMSIS-DAP 错误
