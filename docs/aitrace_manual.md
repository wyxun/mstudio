# AITrace — AI 驱动 MCU 调试 CLI 工具

AITrace 是独立的命令行调试工具，通过 OpenOCD 提供的三个 TCP 通道读取 MCU 调试数据，输出纯文本供人工或 AI 分析。无需 GUI。

## 快速开始

```powershell
# 1. 启动 OpenOCD + RTT（在 modus_template 根目录下）
.\make.bat rtt

# 2. 运行 aitrace（可执行文件位于 ./tools/aitrace.exe）
./tools/aitrace shell help
./tools/aitrace ocd peek 0x20000000
./tools/aitrace wave capture 5
```

## 架构

```
aitrace.exe
├── OpenOCD Telnet  :4444  →  ocd halt|resume|regs|peek|mdw|stack
├── RTT Ch0         :9090  →  shell <命令> | wave start|stop|rate|list
├── RTT Ch1         :9091  →  wave capture <秒数>
├── ELF/MAP 文件    (磁盘) →  map resolve|info | crash report
└── GDB             :3333  →  gdb connect|break|step|print|bt|detach
```

## 侵入等级

| 等级 | 方式 | CPU 影响 | 需确认 |
|------|------|----------|--------|
| **A. 被动** | `shell ...`, `wave ...` | 无 | 否 |
| **B. 暂停** | `ocd regs/peek/mdw/stack` | 暂停约 1 秒 | **是** |
| **C. GDB** | `gdb connect/break/step` | 完全调试控制 | **是** |

**始终优先使用被动命令。** MCU 可能正在驱动电机或功率级电路。

## 命令参考

### 被动命令 — 不打断 CPU

#### shell — 通过 RTT Ch0 发送命令 (TCP 9090)

```powershell
aitrace shell help                 # 列出固件所有 shell 命令
aitrace shell regs                 # 固件端寄存器转储
aitrace shell peek <hex_addr>      # 被动读取内存（uint32）
aitrace shell stack [n]            # 被动栈转储
aitrace shell cfsr                 # 查看故障状态寄存器
aitrace shell list                 # 列出 MODUS 对象
aitrace shell log -E -W -I -D      # 切换日志级别
aitrace shell ver                  # 查看 MODUS 版本
```

固件的日志输出（TICK、Heartbeat 等）会与 shell 响应混在一起。日志量较大时，shell 响应可能被淹没。

#### wave — 波形捕获 (TCP 9091)

```powershell
aitrace wave list                  # 列出当前波形通道
aitrace wave start                 # 开始采集
aitrace wave stop                  # 停止采集
aitrace wave rate 10               # 设置抽取率（1=最快）
aitrace wave capture 5             # 捕获 5 秒输出到 stdout
aitrace wave capture 10 --output w.csv  # 捕获到文件
```

CSV 格式：`时间(µs),通道名1,通道名2,...`，值为 int16 类型。

### 暂停命令 — 短暂暂停 CPU

每个命令自动执行 暂停→读取→恢复，在同一连接上完成。

```powershell
aitrace ocd regs                   # 全部核心寄存器（R0-R12, SP, LR, PC, xPSR, FPU）
aitrace ocd peek 0x20000000        # 读取 SRAM 基地址 uint32
aitrace ocd peek 0x08000000        # 读取 Flash 基地址（初始 SP 值）
aitrace ocd mdw 0x20000000 16      # 从 SRAM 转储 16 个字
aitrace ocd mdw 0x08000000 8       # 从 Flash 转储 8 个字（向量表）
aitrace ocd stack 32               # 转储 SP 周围 32 个字
aitrace ocd halt                   # 仅暂停 CPU（保持暂停状态）
aitrace ocd resume                 # 恢复 CPU
```

示例 — `aitrace ocd regs`：

```
      r0 : 0x0a21fe80
      r1 : 0x00000000
      sp : 0x200057c4
      lr : 0x08004bff
      pc : 0x08004a82
    xPSR : 0x21000000
     msp : 0x200057c4
 primask : 0x00000000
```

示例 — `aitrace ocd mdw 0x08000000 8`：

```
0x08000000: 20005800 08007305 080003c5 08006595
0x08000010: 0800659b 080065a1 080065a7 00000000
```

这是向量表的前 8 个字：
- `20005800` = 初始栈顶指针
- `08007305` = Reset_Handler（含 Thumb 位）
- `080003c5` = NMI_Handler
- `08006595` = HardFault_Handler

### GDB — 完整调试控制（需显式启用）

```powershell
aitrace gdb connect --elf build/template.elf    # 连接 GDB
aitrace gdb break main.c:100                    # 设置断点
aitrace gdb continue                            # 运行到断点
aitrace gdb step                                # 单步执行
aitrace gdb print g_wTickCounter                # 读取变量
aitrace gdb bt                                  # 回溯调用栈
aitrace gdb detach                              # 断开 GDB
```

### 分析命令 — 符号解析与崩溃报告

```powershell
# 地址解析为符号
aitrace map resolve build/template.elf 0x08004a82 0x08004bff 0x08007305
# 输出：
#   0x08004a82 : get_system_ticks + 0x1 [FUNC]
#   0x08004bff : __perfc_is_time_out + 0x16 [FUNC]
#   0x08007305 : Reset_Handler + 0x0 [FUNC]

# 内存段概览
aitrace map info build/template.map
# 输出：段列表（VMA、大小、文件数）、Flash/RAM 总量

# 崩溃分析（必须提供 --pc、--lr、--elf）
aitrace crash report \
    --pc=0x08004a82 \
    --lr=0x08004bff \
    --sp=0x200057c4 \
    --elf=build/template.elf \
    --cfsr=0x00000100

# 可通过 --stack=addr1,addr2,... 提供栈上数据辅助分析
```

## 诊断工作流

### HardFault 分析流程

```
1. 观察 RTT Ch0 输出 —— 固件在异常时自动打印异常帧 + CFSR
2. 从输出中提取 PC、LR、SP
3. aitrace crash report --pc=<PC> --lr=<LR> --sp=<SP> --elf=build/template.elf
4. 解读：
   - PC  → 哪条指令触发了异常
   - LR  → 哪个函数调用了它
   - CFSR → 异常类型（总线/内存/用法/MPU）
```

CFSR 位域解码（`crash report` 内置）：

| CFSR 位 | 异常类型 |
|---------|----------|
| UFSR[0] | 未定义指令 |
| UFSR[1] | 无效状态 |
| UFSR[2] | 无效 PC 加载 |
| UFSR[5] | 除零错误 |
| UFSR[6] | 非对齐访问 |
| BFSR[0] | 指令总线错误 |
| BFSR[1] | 精确数据总线错误 |
| BFSR[3] | 出栈总线错误 |
| BFSR[4] | 入栈总线错误 |
| MMSR[0] | 指令 MPU 违规 |
| MMSR[1] | 数据 MPU 违规 |

### 运行时行为分析

```
1. aitrace wave capture 5 > wave.csv     # 捕获波形
2. aitrace ocd regs                       # 检查 CPU 状态
3. 分析 CSV：查找异常值，与预期范围对比
```

### 变量查看方式对比

| 方式 | 命令 | 侵入性 |
|------|------|--------|
| 被动 | `aitrace shell peek <地址>` | 无 |
| 暂停 | `aitrace ocd peek <地址>` | 暂停约 1 秒 |
| GDB | `aitrace gdb connect --elf ...` + `print <变量>` | 完全控制 |

## 环境要求

- **OpenOCD** 已启用 RTT（端口 4444、9090、9091）
- **ELF 文件** 用于符号解析（如 `build/template.elf`）
- **MAP 文件** 用于段分析（如 `build/template.map`）
- **gdb-multiarch** 在 PATH 中（GDB 命令需要）
- **Windows**: MSYS2/MinGW-w64 运行时

当前支持芯片：STM32G431、AT32F421（均为 Cortex-M4）。

## 构建

```bash
# 在 E:\Project\mstudio\aitrace 目录下
make clean-all && make

# 产物：aitrace.exe（约 2MB，链接 libmstudiocore.a + ws2_32）
# 部署：拷贝到 modus_template\tools\ 目录下
cp aitrace.exe E:\Project\modus_template\tools\
```

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 用法错误或连接失败 |
| 3 | 工具内部异常（请报告 bug） |

## 常见问题

### CMSIS-DAP 错误

如果看到 `error writing data: Input/Output Error` 和 `CMSIS-DAP command failed`：

```powershell
# 检查是否有多个 OpenOCD 实例
tasklist | findstr openocd

# 全部终止后重新启动
taskkill /F /IM openocd.exe
.\make.bat rtt
```

两个 OpenOCD 进程争抢同一个调试探针会导致 CMSIS-DAP 通信失败。

### RTT Shell 无响应

固件的 shell 命令（如 `regs`、`help`）通过 RTT Ch0 下通道传输。如果固件的 shell 任务优先级较低或正在处理大量日志输出，命令响应可能延迟或被日志淹没。此时应使用 `aitrace ocd regs`（通过 OpenOCD Telnet 直接读取，更可靠）。

## 安全守则

1. **被动优先**：始终先尝试 `shell` 和 `wave` 命令
2. **暂停/GDB 需确认**：CPU 暂停会中断实时控制环路
3. **未经工程师确认，绝不修改固件或重新烧录**
4. **检查重复 OpenOCD 实例**：两个 OpenOCD 争抢调试探针会导致 CMSIS-DAP 错误
