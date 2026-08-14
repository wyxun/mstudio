# DSH 集成设计：MCU 调试 + 电路设计工作台

日期：2026-08-14
状态：设计已批准，待实现计划

## 1. 背景与目标

电子工程师日常两件事：调 MCU（电机控制，FOC）和画板（KiCad 电源/模拟电路）。目标是把这两件事接进 DeepSeek Harness（DSH）——AI 不仅能驱动调试工具干活，而且**无需用户提醒**就知道工具怎么用、项目什么状态、上次查到哪。

三个集成域：

- **域 A：MCU 调试** — 把 `aitrace.exe`（AI 驱动的 MCU 调试 CLI）封装为 DSH 工具集，接入 DSH 审批机制；
- **域 B：电路设计** — KiCad 工程语义化读取 + 精确计算 + 拓扑绘制 + kicad-auditor 确定性审计；
- **记忆层** — skill 自动发现 + 调试/设计笔记自写自读 + 会话持久化。

## 2. 总体架构

```
DSH Web GUI 会话（一个会话，按任务切换工作目录）
│
├─ 域 A：MCU 调试（cwd = modus_template）
│   ├─ aitrace_* 工具集（DSH 插件，subprocess 调 aitrace.exe）
│   ├─ skill: aitrace（frontmatter 后 DSH 自动发现）
│   └─ notes: 调试记忆
│
├─ 域 B：电路设计（cwd = 任意 KiCad 工程）
│   ├─ kicad-auditor 工具（sch/param/pcb/run，JSON 输出，确定性引擎）
│   ├─ 华秋 KiCad MCP（dsh-mcp-client 直连，读 netlist/绘制/ERC/仿真）
│   ├─ 本地工具（sch 语义化摘要、circuit_calc 精确计算、SVG 拓扑）
│   └─ skill: kicad-auditor 协同模式
│
└─ 记忆层：notes 自写自读 + 会话持久化（跨域共享）
```

外部依赖：

- OpenOCD RTT（TCP 4444/9090/9091）+ 调试探针（SWD）
- `aitrace.exe`（modus_template/tools/，已编译）
- KiCad 华秋版 9.0.7（D:\0_software\KiCad\9.0，含 kicad-cli 与 kicad-mcp-server）
- kicad-auditor（本地 C++ 工程，MSYS2 + clang++ 构建，尚未编译）

## 3. 域 A：aitrace 工具集

### 3.1 工具清单

按侵入等级分三组（对应 aitrace_manual.md 安全分级）：

| 组 | 工具 | aitrace 子命令 | 审批 |
|---|---|---|---|
| 被动 | `aitrace_shell` | `shell [--raw] <cmd...>` | 无 |
| 被动 | `aitrace_wave_stat` | `wave stat [sec]` | 无 |
| 被动 | `aitrace_wave_capture` | `wave capture <sec> [--output]` | 无 |
| 被动 | `aitrace_wave_control` | `wave list/start/stop/rate <n>` | 无 |
| 被动 | `aitrace_serial` | `serial --port --baud [--send]...` | 无 |
| 被动 | `aitrace_map_resolve` | `map resolve <elf> <addr...>` | 无 |
| 被动 | `aitrace_map_info` | `map info <elf_or_map>` | 无 |
| 被动 | `aitrace_crash_report` | `crash report --pc --lr --sp --elf` | 无 |
| 暂停 | `aitrace_ocd_*` | `ocd regs/peek/mdw/stack/halt/resume` | 每次确认 |
| GDB | `aitrace_gdb_*` | `gdb connect/break/continue/step/print/bt/detach` | 每次确认 |

### 3.2 结构化输出

- `aitrace_wave_stat`：解析 stdout 为 `{rate, crc_err, seq_lost, summary}`；
- `aitrace_wave_capture`：CSV 落盘到 `captureDir`，返回 `{csvPath}`，UI 卡片带文件位置；
- `aitrace_crash_report`：返回结构化故障信息（PC/LR/SP 符号化、CFSR 位域解码）；
- 其余返回规范化文本 + 退出码。

### 3.3 审批绑定

暂停/GDB 组工具 `execute()` 先调 `ctx.approval.request()`，`allowed-once` 才执行；拒绝返回明确错误。工具 `description` 写明侵入等级（模型在选择阶段即知哪些要问）。`approval` 配置开关可整体禁用（默认 true）。

### 3.4 配置（cordis patch）

```yaml
aitracePath: tools/aitrace.exe   # 相对 workDir
workDir: D:/2_xundoc/project/modus_template
elfPath: build/template.elf
mapPath: build/template.map
captureDir: captures/
shellTimeoutMs: 15000
approval: true
```

ELF/MAP 实际路径以 target.mk 为准，配置可覆盖。

## 4. 域 B：KiCad 电路设计

### 4.1 AI 读原理图：三层语义化通道

**不直接喂 `.kicad_sch` 原始文本给 AI**（实测效果差：图形噪音占 90%，连接关系埋在坐标几何中）。改为：

| 层 | 通道 | 输出 | 适用 |
|---|---|---|---|
| ① 全局摘要 | `kicad-auditor sch -i <sch> -j` | 元件表 + 违规列表（JSON） | 一次看清全板 |
| ② 局部深入 | `kicad-auditor param <ref> <sch> -j` | 单元件引脚级网络连接 + 邻近器件 | 讨论单个电路 |
| ③ 网表兜底 | `kicad-cli sch export netlist` / 华秋 MCP `get_netlist` | 标准网表 | KiCad 打开时实时读 |

### 4.2 计算：circuit_calc 工具

精确数值计算走 Python（非 LLM 心算）：

- 反馈分压：`Vout = VREF × (1 + R1/R2)`，含电阻精度误差带、FB 偏置电流修正、前馈电容影响；
- 电源拓扑：buck/boost 伏秒平衡、电感纹波、输出电容纹波、环路极点估算；
- 滤波器：RC/LC 截止频率、Sallen-Key 传递函数、运放增益配置。

### 4.3 绘制：SVG 为主 + 华秋 MCP 为辅

- **SVG 拓扑图**：DSH 会话内渲染（markdown 图片），零风险快速迭代；AI 生成电路拓扑（buck、FB 分压网络、RC 滤波…）；
- **华秋 MCP 绘制**（进阶）：`place_symbol` / `draw_multi_wires` / `create_local_label` 在 KiCad 里直接画。写操作每次弹窗确认 + **不自动保存**（`saveFrame` 由用户决定）。默认关闭，配置 `mcpDraw: false`。

### 4.4 华秋 KiCad MCP 接入

- DSH `dsh-mcp-client` 以 stdio 方式连接 `kicad-mcp-server`：`uv run main.py <ipc-socket-url>`（uv.exe 在 KiCad bin 下）；
- socket URL 来自华秋版 KiCad 的 copilot/SDK 服务（实现期验证获取方式）；
- 工具命名 `mcp__kicad__<name>`，只读工具（get_netlist/query_*）放行，写工具（place_*/draw_*/modify_*/create_*）审批；
- 前提：KiCad 运行中。

### 4.5 kicad-auditor 工具封装

| 工具 | 命令 | 用途 |
|---|---|---|
| `audit_sch` | `sch -i <sch> [-j]` | 全板原理图安全审计（隔离/FB/规格） |
| `audit_param` | `param <ref> <sch> [-j]` | 单元件连接分析（设计讨论入口） |
| `audit_pcb` | `pcb -i <pcb>` | PCB 高频 clearance/EMI 审计 |
| `audit_run` | `run -i <pcb> [-c mm] [-o md]` | 联合审计 + Markdown 报告 |

实现期先在 MSYS2 配置 clang++ 并 `make.bat` 构建（README 承诺 79 项自测通过），再用现有仿真工程（如 Buck）验证输出。

## 5. 知识层与记忆（"无需提醒"）

### 5.1 skill 自动发现

- `modus_template/.agents/skills/aitrace/SKILL.md` 加 frontmatter（name/description/whenToUse），DSH `skill-filesystem` 按项目根自动发现（rank 200），无需搬迁；
- 正文补"DSH 工具映射"：有 `aitrace_*` 工具时优先用工具（自带审批/校验），不手敲 bash；
- `.claude/skills/aitrace-skill.md` 兼容入口保留不动；
- 新增 `kicad-auditor` skill（协同模式）：设计讨论时自动加载，调 `audit_param` 拿真实连接再分析；
- KiCad 工程根放轻量 AGENTS.md 或 skill 索引，说明工程结构（sch/pcb 位置、电源树）。

### 5.2 notes 自写自读

AI 每次调试/设计会话后在 `.agents/notes/` 写记录：链路状态、验收数据、踩坑、FB 网络参数、待办。下次会话自动参考（DSH 会话持久化 + notes 文件）。

### 5.3 会话组织

- 调试会话 cwd = modus_template；设计会话 cwd = KiCad 工程；
- 一个 profile（`web` 默认 + 工具插件挂载）服务两个域；skill 按任务上下文加载。

## 6. 验证场景

### S1 波形链路质量验收（域 A，端到端）

用户"验收一下波形链路" → AI 自动：加载 skill → 查 OpenOCD 进程 → `aitrace_wave_stat 5` → 对照验收标准（rate≈1000、crc_err=0、seq_lost=0）→ 交叉验证 `shell wave drop` → 写 notes。

### S2 FB 分压校验（域 B，端到端）

用户"校验 1.8V 电源 FB 分压" → AI：`audit_sch -j` 定位 FB 网络 → `audit_param <ref>` 提取 R1/R2 实际值 → `circuit_calc` 算 Vout ± 误差带 → 结论 + 改值建议（只读不改文件）→ 可选 SVG 拓扑图。

### S3 审计驱动改板（域 B）

用户"跑一下全板审计" → AI：`audit_run` → 解读 Markdown/JSON 报告 → 按严重级给改板建议清单。

## 7. 二期明确不做

- aitrace 加 `--json` 输出（工具解析暴露痛点再做）；
- mstudio GUI 命令行联动（argv 打开 CSV）；
- AI 直接修改 `.kicad_sch` 文件（写回风险高，待 MCP 绘制成熟后评估）；
- ACP 反向集成（mstudio 驱动 DSH）。

## 8. 验收标准

1. 域 A 工具集在 DSH Web GUI 可见可调，参数校验生效；B/C 级工具每次弹窗确认，拒绝路径正确；
2. skill 被 DSH 自动发现（不提醒即加载）；
3. 域 B 三层读取通道可用（sch 摘要 / param 局部 / netlist 兜底），KiCad 未打开时离线通道可用；
4. `circuit_calc` 对 FB 分压给出带误差带的精确结果；
5. kicad-auditor 构建成功且 79 项自测通过，S3 审计报告可产出；
6. S1/S2/S3 三个场景端到端跑通，notes 落盘。

## 9. 里程碑

- **M1**：skill frontmatter + 验证自动发现（30 分钟）
- **M2**：域 A 工具集 bundle 开发 + 审批接入（约 1 天）
- **M3**：kicad-auditor 构建验证 + 域 B 工具封装（约 1 天）
- **M4**：circuit_calc + SVG 拓扑 + 华秋 MCP 接入（约 1 天）
- **M5**：S1/S2/S3 端到端验证 + notes 沉淀（半天）
