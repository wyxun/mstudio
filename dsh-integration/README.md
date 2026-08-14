# mstudio-dsh — DeepSeek Harness 集成包

电子工程师工作台：MCU 调试（aitrace）+ 电路设计（kicad-auditor / 计算 / 拓扑图）。

## 安装（一次性的）

1. **构建**：`mstudio/dsh-integration/` 下运行 `pnpm run build`（tsc 产物到 `lib/`）。
2. **部署**：复制 `lib/` 与 `package.json` 到
   `C:\Users\Administrator\.dsh\profiles\web\node_modules\@wx\mstudio-dsh\`。
3. **挂载**：`~\.dsh\profiles\web\cordis.patch.yml` 含 `mstudio-aitrace` insert 行
   （workDir = modus_template）。
4. **重启 DSH Web GUI**，新会话即可见全部工具。

开发循环：改 `src/` → `pnpm run build` → 重新复制 `lib/` → 重启 GUI。

## 配置（cordis.patch.yml）

| 字段 | 默认 | 含义 |
|---|---|---|
| `workDir` | `D:/2_xundoc/project/modus_template` | 固件工程根 |
| `aitracePath` | `tools/aitrace.exe` | aitrace 可执行（相对 workDir） |
| `elfPath` / `mapPath` | `build/template.elf` / `.map` | 符号解析默认文件 |
| `captureDir` | `captures` | 波形 CSV 落盘目录 |
| `approval` | `true` | 暂停/GDB 工具审批总开关 |
| `shellTimeoutMs` | `15000` | 单次调用超时 |
| `auditorPath` | `D:/2_xundoc/project/kicad-auditor/kicad-auditor.exe` | 审计引擎 |

## 工具清单（28 个）

- **aitrace 被动（8）**：`aitrace_shell` `aitrace_wave_stat` `aitrace_wave_capture`
  `aitrace_wave_control` `aitrace_serial` `aitrace_map_resolve` `aitrace_map_info`
  `aitrace_crash_report`
- **aitrace 审批（13）**：`aitrace_ocd_regs/peek/mdw/stack/halt/resume`、
  `aitrace_gdb_connect/break/continue/step/print/bt/detach`（每次调用弹窗确认）
- **kicad-auditor（4）**：`audit_sch` `audit_param` `audit_pcb` `audit_run`
- **计算/绘图（2）**：`circuit_calc` `svg_topology`
- **华秋 KiCad MCP**（KiCad 运行时挂载，写操作弹窗确认）：见
  `docs/kicad-mcp-connect.md`

## Skill（自动发现，无需提醒）

- `modus_template/.agents/skills/aitrace/SKILL.md`：MCU 调试权威流程（含会话启动
  环境自检：AI 调试前自动查 OpenOCD/探针/ELF）
- `~/.agents/skills/kicad-auditor/SKILL.md`：电路设计协同（三层读取、FB 校验
  工作流、只读+建议守则）

## 常见任务速查

| 你说 | AI 做 |
|---|---|
| 验收一下波形链路 | wave stat 三指标 + 交叉验证 wave drop |
| 板子挂了帮我查 | 提取 PC/LR/SP → crash report → map resolve |
| 校验 X 的 FB 分压 | audit_param 提取真实阻值 → circuit_calc 精确计算 → 方案表 |
| 画个 buck 拓扑 | svg_topology 会话内渲染 |
| 跑全板审计 | audit_run → Markdown 报告 → 按严重级给建议 |

## 已知限制

- 华秋 MCP 需要 KiCad 运行中；socket URL 每次 KiCad 启动会变（见 SOP）
- 波形微观测量建议用 mstudio GUI 人肉操作
- 不替工程师烧录固件、不修改 KiCad 工程文件
