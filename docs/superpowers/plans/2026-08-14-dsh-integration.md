# DSH 集成实现计划：MCU 调试 + 电路设计工作台

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 aitrace（MCU 调试）与 KiCad（电路设计）两套能力接进 DeepSeek Harness，AI 驱动工具干活且无需提醒。

**Architecture:** 一个独立 npm bundle（`mstudio/dsh-integration/`）通过 cordis patch 挂载到 DSH profile；域 A 工具经 `ctx.subprocess` 调 `aitrace.exe`，B/C 级命令经 `ctx.approval` 每次确认；域 B 用 kicad-auditor（确定性审计）+ 自研计算/SVG 工具 + 华秋 KiCad MCP（dsh-mcp-client）；知识层靠 skill frontmatter 自动发现 + notes 自写自读。

**Tech Stack:** TypeScript（ESM）、@deepseek-ai/cordis、@deepseek-ai/dsh-tools（defineTool）、@deepseek-ai/dsh-subprocess、@deepseek-ai/dsh-user-approval、vitest、aitrace.exe（C++，已编译）、kicad-auditor（C++20，需构建）、华秋 KiCad 9.0.7（含 kicad-cli + kicad-mcp-server + uv.exe）。

**Spec:** `docs/superpowers/specs/2026-08-14-dsh-integration-design.md`

**约定：**
- 包根 `D:\2_xundoc\project\mstudio\dsh-integration\`（下文写 `<PACKAGE>/`）；DSH 仓库根写 `<DSH>/`（`D:\2_xundoc\project\deepseek-harness`）。
- 所有新增 TS 文件为 ESM（`"type": "module"`），本地相对导入带 `.ts` 后缀。
- 每个任务独立可提交；测试优先（TDD），硬件/MCP 依赖步骤用显式验证脚本。

---

## Phase 0：知识层前置（M1，30 分钟）

### Task 1: 给 aitrace skill 加 frontmatter 与工具映射

**Files:**
- Modify: `D:\2_xundoc\project\modus_template\.agents\skills\aitrace\SKILL.md`

- [ ] **Step 1: 在 SKILL.md 顶部插入 frontmatter 与映射小节**

在文件第一行 `# AITrace Debug Skill` 之前插入：

```markdown
---
name: aitrace
description: MCU 调试权威流程：tools/aitrace.exe 与 OpenOCD RTT 交互（shell/wave/ocd/gdb/map/crash/serial），含侵入分级与安全守则。
whenToUse: 需要调试固件行为、HardFault 定位、波形链路质量验收、变量/内存/寄存器检查、串口闭环联调时
---

## DSH Tool Mapping

In DeepSeek Harness sessions, prefer the `aitrace_*` tools (they carry schema
validation and built-in approval for halt/GDB commands) over hand-typing
`aitrace.exe` in bash. Passive commands need no confirmation; `aitrace_ocd_*`
and `aitrace_gdb_*` always prompt the engineer first.
```

- [ ] **Step 2: 验证 frontmatter 合法**

Run: `node -e "const y=require('js-yaml');console.log(Object.keys(y.load(require('fs').readFileSync('D:/2_xundoc/project/modus_template/.agents/skills/aitrace/SKILL.md','utf8').split('---')[1])))"`
Expected: 打印 `[ 'name', 'description', 'whenToUse' ]`，无异常。

- [ ] **Step 3: 提交**

```bash
git -C D:/2_xundoc/project/modus_template add .agents/skills/aitrace/SKILL.md
git -C D:/2_xundoc/project/modus_template commit -m "docs: add DSH frontmatter and tool mapping to aitrace skill"
```

### Task 2: 验证 DSH skill 自动发现

- [ ] **Step 1: 准备一个 DSH 会话指向 modus_template**

用现有 Web GUI 新建会话，将会话工作目录设为 `D:\2_xundoc\project\modus_template`（在 GUI 会话设置中配置 cwd；若 GUI 无此设置，则用 `dsh --profile web` 启动时 `--patch` 指定，见 Task 7 的 patch 技术）。

- [ ] **Step 2: 让模型加载 skill 并确认内容**

会话中输入：`查看 aitrace skill 的内容，并说明它的安全分级规则`。
Expected: 模型通过 skill 工具加载了 `aitrace`（description 与 frontmatter 一致），并能复述被动/暂停/GDB 三级与"每次确认"规则。若模型报"找不到该 skill"，检查 DSH 日志中 skill-filesystem 的 discovery 行，确认项目根识别为 modus_template（含 `.git` 的最近祖先）。

- [ ] **Step 3: 提交**（如无代码改动则跳过）

---

## Phase 1：域 A aitrace 工具集（M2，约 1 天）

### Task 3: bundle 骨架与挂载

**Files:**
- Create: `<PACKAGE>/package.json`
- Create: `<PACKAGE>/tsconfig.json`
- Create: `<PACKAGE>/cordis.patch.yml`
- Create: `<PACKAGE>/src/index.ts`（空聚合入口，后续任务填充）

- [ ] **Step 1: 创建 package.json**

```json
{
  "name": "@wx/mstudio-dsh",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "main": "./lib/index.js",
  "types": "./lib/types/index.d.ts",
  "dsh": { "bundle": { "patch": "./cordis.patch.yml" } },
  "scripts": {
    "build": "tsc -p tsconfig.json",
    "test": "vitest run"
  },
  "dependencies": {
    "@deepseek-ai/cordis": "workspace:*",
    "@deepseek-ai/dsh-tools": "workspace:*",
    "@deepseek-ai/dsh-subprocess": "workspace:*",
    "@deepseek-ai/dsh-user-approval": "workspace:*",
    "@deepseek-ai/schemastery": "workspace:*"
  },
  "devDependencies": {
    "typescript": "^5.5.0",
    "vitest": "^3.0.0",
    "@deepseek-ai/dsh-tool-bash": "workspace:*"
  }
}
```

- [ ] **Step 2: 创建 tsconfig.json**

```json
{
  "extends": "../../deepseek-harness/tsconfig.base.json",
  "compilerOptions": {
    "rootDir": "src",
    "outDir": "lib/types",
    "paths": {
      "@deepseek-ai/*": ["../../deepseek-harness/packages/*/lib/types"]
    }
  },
  "include": ["src"]
}
```

若 extends 路径解析失败，改为复制 DSH 仓库 `tsconfig.base.json` 内容并调整 `paths` 指向 `../../deepseek-harness/packages/*/lib/types`。

- [ ] **Step 3: 创建 cordis.patch.yml**

```yaml
# mstudio-dsh bundle patch: mounts the aitrace and kicad tool plugins.
- insert:
    - id: mstudio-aitrace
      name: '@wx/mstudio-dsh'
      config:
        workDir: D:/2_xundoc/project/modus_template
        aitracePath: tools/aitrace.exe
        elfPath: build/template.elf
        mapPath: build/template.map
        captureDir: captures
        approval: true
```

- [ ] **Step 4: 创建空聚合入口**

`<PACKAGE>/src/index.ts`：

```ts
// Aggregated plugin entry. Domain plugins register in this file's apply().
import type { Context } from '@deepseek-ai/cordis'

export const name = 'mstudio-dsh'
export const inject = ['tools', 'subprocess']

export interface Config {
  workDir: string
  aitracePath: string
  elfPath: string
  mapPath: string
  captureDir: string
  approval: boolean
}

export const Config = undefined as never // replaced by schemastery schema in Task 4

export function apply(ctx: Context) {
  // Task 5/6 register aitrace tools; Task 10/11/12 register kicad tools.
}
```

- [ ] **Step 5: 安装依赖并构建**

Run（在 `<PACKAGE>/`）: `pnpm install`（在 DSH 仓库已 `pnpm install` 的前提下，用 `pnpm add file:../../deepseek-harness` 的 workspace 语义按需调整；目标是 `node_modules` 里能解析 `@deepseek-ai/*`）→ `pnpm run build`
Expected: `lib/index.js` 与 `lib/types/index.d.ts` 生成，无类型错误。

- [ ] **Step 6: 挂载到 DSH profile 并确认加载**

Run（在 `<DSH>/`）: `pnpm dsh --profile web --patch ../mstudio/dsh-integration/cordis.patch.yml --dump-config | findstr mstudio`
Expected: 输出中包含 `mstudio-aitrace` 行。若 patch 语法报错，对照 `<DSH>/packages/bundle/base/cordis.patch.yml` 的 insert 块结构修正。

- [ ] **Step 7: 提交**

```bash
git -C D:/2_xundoc/project/mstudio add dsh-integration
git -C D:/2_xundoc/project/mstudio commit -m "feat(dsh): scaffold mstudio-dsh bundle skeleton"
```

### Task 4: aitrace runner 与配置 schema

**Files:**
- Create: `<PACKAGE>/src/aitrace/runner.ts`
- Create: `<PACKAGE>/src/aitrace/config.ts`
- Create: `<PACKAGE>/tests/aitrace/runner.test.ts`

- [ ] **Step 1: 写失败测试（argv 构造与退出码分类）**

`<PACKAGE>/tests/aitrace/runner.test.ts`：

```ts
import { describe, expect, it } from 'vitest'
import { buildAitraceArgv } from '../../src/aitrace/runner.ts'

describe('buildAitraceArgv', () => {
  it('builds passive shell argv with workdir-relative exe', () => {
    const argv = buildAitraceArgv(
      { workDir: 'D:/proj', aitracePath: 'tools/aitrace.exe' },
      ['shell', 'regs'],
    )
    expect(argv).toEqual(['D:/proj/tools/aitrace.exe', 'shell', 'regs'])
  })

  it('builds capture argv with output file', () => {
    const argv = buildAitraceArgv(
      { workDir: 'D:/proj', aitracePath: 'aitrace' },
      ['wave', 'capture', '5', '--output', 'cap.csv'],
    )
    expect(argv[argv.length - 1]).toBe('cap.csv')
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run（在 `<PACKAGE>/`）: `pnpm test`
Expected: FAIL，`cannot find module '../../src/aitrace/runner.ts'`。

- [ ] **Step 3: 实现 config 与 runner**

`<PACKAGE>/src/aitrace/config.ts`：

```ts
import z from '@deepseek-ai/schemastery'
import { resolve } from 'node:path'

export const AitraceConfig = z.object({
  workDir: z.string().default('D:/2_xundoc/project/modus_template'),
  aitracePath: z.string().default('tools/aitrace.exe'),
  elfPath: z.string().default('build/template.elf'),
  mapPath: z.string().default('build/template.map'),
  captureDir: z.string().default('captures'),
  approval: z.boolean().default(true),
  shellTimeoutMs: z.number().default(15000),
})
export type AitraceConfigT = z.infer<typeof AitraceConfig>

export function resolveAitraceExe(cfg: AitraceConfigT): string {
  return resolve(cfg.workDir, cfg.aitracePath)
}
```

`<PACKAGE>/src/aitrace/runner.ts`：

```ts
import type { Context } from '@deepseek-ai/cordis'
import type { SubprocessOutcome } from '@deepseek-ai/dsh-subprocess'
import { resolve } from 'node:path'
import type { AitraceConfigT } from './config.ts'

export function buildAitraceArgv(
  cfg: AitraceConfigT,
  args: string[],
): string[] {
  return [resolve(cfg.workDir, cfg.aitracePath), ...args]
}

export interface AitraceRunResult {
  stdout: string
  stderr: string
  exitCode: number | null
  signal: NodeJS.Signals | null
}

/**
 * Spawn one aitrace invocation through ctx.subprocess with bounded collected
 * output; throws on timeout/abort, returns exit facts otherwise. The caller
 * decides whether a non-zero exit is an error result (throw) or a domain
 * outcome (return) per tool.
 */
export async function runAitrace(
  ctx: Context,
  cfg: AitraceConfigT,
  args: string[],
  opts: { timeoutMs?: number; signal?: AbortSignal } = {},
): Promise<AitraceRunResult> {
  const timeoutMs = opts.timeoutMs ?? cfg.shellTimeoutMs
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  const outer = opts.signal
  if (outer) {
    if (outer.aborted) controller.abort()
    else outer.addEventListener('abort', () => controller.abort(), { once: true })
  }
  try {
    const handle = ctx.subprocess.spawn({
      argv: buildAitraceArgv(cfg, args),
      cwd: cfg.workDir,
      stdio: {
        stdin: 'ignore',
        stdout: { maxBytes: 4 * 1024 * 1024, spill: { maxBytes: 8 * 1024 * 1024 } },
        stderr: { maxBytes: 1024 * 1024 },
      },
      graceMs: 2000,
      signal: controller.signal,
    })
    const outcome: SubprocessOutcome = await handle.outcome
    const read = await handle.collected.readOutput(0)
    return {
      stdout: read.text,
      stderr: '', // collected via handle.collected.stderr in production build; see note
      exitCode: outcome.exitCode,
      signal: outcome.signal,
    }
  } finally {
    clearTimeout(timer)
  }
}
```

注：`SubprocessHandle.collected` 的实际读取 API 以 `<DSH>/packages/subprocess/subprocess/src/types.ts` 的 `SubprocessCollectedOutputs` 为准（含 `stdout`/`stderr` 两个 `SubprocessOutputReader`）；若 `readOutput(0)` 命名不同，按类型定义修正为实际方法名，并让测试覆盖。

- [ ] **Step 4: 运行确认通过**

Run: `pnpm test`
Expected: PASS（2 个用例）。

- [ ] **Step 5: 提交**

```bash
git -C D:/2_xundoc/project/mstudio add dsh-integration
git -C D:/2_xundoc/project/mstudio commit -m "feat(dsh): add aitrace runner with config schema"
```

### Task 5: 被动工具组（shell / wave / serial / map / crash）

**Files:**
- Create: `<PACKAGE>/src/aitrace/passive.ts`
- Create: `<PACKAGE>/tests/aitrace/parse.test.ts`

- [ ] **Step 1: 写失败测试（wave stat 解析）**

`<PACKAGE>/tests/aitrace/parse.test.ts`：

```ts
import { describe, expect, it } from 'vitest'
import { parseWaveStat } from '../../src/aitrace/passive.ts'

describe('parseWaveStat', () => {
  it('parses the three link-quality metrics and summary', () => {
    const text = [
      'rate 999.924 f/s   crc_err 0   seq_lost 0',
      'rate 998.1 f/s   crc_err 1   seq_lost 2',
      'summary: 3032 frames (50.001 KB total), crc_err 1, seq_lost 2',
    ].join('\n')
    const r = parseWaveStat(text)
    expect(r.rate).toBeCloseTo(998.1)
    expect(r.crc_err).toBe(1)
    expect(r.seq_lost).toBe(2)
    expect(r.summary).toContain('3032 frames')
  })

  it('returns null for unrecognized output', () => {
    expect(parseWaveStat('nothing here')).toBeNull()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `pnpm test`
Expected: FAIL，模块不存在。

- [ ] **Step 3: 实现 passive.ts（8 个被动工具 + 解析器）**

`<PACKAGE>/src/aitrace/passive.ts` 核心内容（完整文件包含全部 8 个工具，此处给出骨架与两个代表实现，其余按同一模式补齐）：

```ts
import type { Context } from '@deepseek-ai/cordis'
import { defineTool } from '@deepseek-ai/dsh-tools'
import { join } from 'node:path'
import type { AitraceConfigT } from './config.ts'
import { runAitrace } from './runner.ts'

export interface WaveStatResult {
  rate: number | null
  crc_err: number | null
  seq_lost: number | null
  summary: string | null
}

export function parseWaveStat(text: string): WaveStatResult | null {
  const rateMatch = /rate\s+([\d.]+)\s+f\/s/.exec(text)
  const crcMatch = /crc_err\s+(\d+)/.exec(text)
  const seqMatch = /seq_lost\s+(\d+)/.exec(text)
  const summaryMatch = /summary:\s+(.+)/.exec(text)
  if (!rateMatch && !crcMatch && !seqMatch) return null
  return {
    rate: rateMatch ? Number(rateMatch[1]) : null,
    crc_err: crcMatch ? Number(crcMatch[1]) : null,
    seq_lost: seqMatch ? Number(seqMatch[1]) : null,
    summary: summaryMatch ? summaryMatch[1] : null,
  }
}

export function registerPassiveAitraceTools(ctx: Context, cfg: AitraceConfigT): void {
  ctx.tools.register(defineTool({
    name: 'aitrace_shell',
    description:
      'Send a shell command to the MCU over RTT Ch0 (passive, no CPU interruption). '
      + 'Use for firmware shell commands: regs, peek <hex_addr>, stack [n], cfsr, list, ver, log, wave drop. '
      + 'Default filters [T]-level log lines; set raw=true to disable filtering.',
    parameters: {
      command: { type: 'string', required: true, description: 'Shell command, e.g. "regs" or "peek 0x20000000"' },
      raw: { type: 'boolean', description: 'Disable [T] log filtering' },
    },
    output: {
      schema: { type: 'string' },
      render: (_args, value) => [{ type: 'text', text: value }],
    },
    async execute(args, exec) {
      const argv = args.raw ? ['shell', '--raw', args.command] : ['shell', args.command]
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace shell failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_wave_stat',
    description:
      'Waveform link-quality statistics over RTT Ch1 (passive). Returns rate (f/s), crc_err, seq_lost. '
      'Acceptance: rate≈push rate (1000 for 1kHz), crc_err=0, seq_lost=0.',
    parameters: {
      seconds: { type: 'number', description: 'Observation window in seconds (default 5)' },
    },
    output: {
      schema: {
        type: 'object',
        properties: {
          rate: { type: 'number' },
          crc_err: { type: 'number' },
          seq_lost: { type: 'number' },
          summary: { type: 'string' },
        },
        additionalProperties: false,
      },
      render: (_a, v: WaveStatResult) => [{
        type: 'text',
        text: `rate=${v.rate} f/s, crc_err=${v.crc_err}, seq_lost=${v.seq_lost}\n${v.summary ?? ''}`,
      }],
    },
    async execute(args, exec) {
      const seconds = args.seconds ?? 5
      const r = await runAitrace(ctx, cfg, ['wave', 'stat', String(seconds)], { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace wave stat failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      const parsed = parseWaveStat(r.stdout)
      if (!parsed) throw new Error(`unrecognized wave stat output:\n${r.stdout}`)
      return parsed
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_wave_capture',
    description:
      'Capture waveform to CSV over RTT Ch1 (passive). Saves to captureDir with timestamp and returns the path.',
    parameters: {
      seconds: { type: 'number', required: true, description: 'Capture duration in seconds' },
      channels: { type: 'string', description: 'Optional channel list (comma separated); empty captures all' },
    },
    output: {
      schema: { type: 'object', properties: { csvPath: { type: 'string' } }, additionalProperties: false },
      render: (_a, v: { csvPath: string }) => [{ type: 'text', text: `Captured to ${v.csvPath}` }],
    },
    async execute(args, exec) {
      const name = `wave_${new Date().toISOString().replace(/[:.]/g, '-')}.csv`
      const out = join(cfg.workDir, cfg.captureDir, name)
      const argv = ['wave', 'capture', String(args.seconds), '--output', out]
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal, timeoutMs: (args.seconds + 20) * 1000 })
      if (r.exitCode !== 0) throw new Error(`aitrace wave capture failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return { csvPath: out }
    },
  }))

  // aitrace_wave_control: wave list|start|stop|rate <n> — one tool, action param.
  // aitrace_serial: serial --port <p> --baud <b> [--send <s>] [--send-hex <h>] [--duration <sec>] [--hex] [--ascii]
  // aitrace_map_resolve: map resolve <elf> <addr...> — args: addresses[] (hex strings), returns raw text.
  // aitrace_map_info: map info <elf_or_map> — returns section summary text.
  // aitrace_crash_report: crash report --pc --lr --sp --elf [--cfsr] — returns decoded report text.
}
```

其余 6 个工具按以下精确规格实现（模式与上两个一致：参数 schema → `runAitrace` → 非零退出抛错 → 返回结构化或文本结果）：

| 工具 | 参数 schema | CLI 映射 |
|---|---|---|
| `aitrace_wave_control` | `action: enum['list','start','stop','rate'] required; rate?: number` | `wave <action>`；action=rate 时追加 `String(rate)`；输出原样文本 |
| `aitrace_serial` | `port: string required; baud: number default 115200; send?: string; sendHex?: string; duration?: number default 5; hexOutput?: boolean` | `serial --port <port> --baud <baud>` + `--send <send>` 或 `--send-hex <sendHex>` + `--duration <duration>` + `--hex`（hexOutput） |
| `aitrace_map_resolve` | `addresses: string[] required（hex 字符串，如 "0x08004a82"）; elf?: string 默认 cfg.elfPath` | `map resolve <elf> <addr...>`；输出原样文本 |
| `aitrace_map_info` | `target?: string 默认 cfg.mapPath（.map 或 .elf 均可）` | `map info <target>`；输出原样文本 |
| `aitrace_crash_report` | `pc/lr/sp: string required; elf?: string 默认 cfg.elfPath; cfsr?: string（hex）` | `crash report --pc=<pc> --lr=<lr> --sp=<sp> --elf=<elf>` + `--cfsr=<cfsr>`；输出报告文本 |
| `aitrace_wave_capture`（已实现） | 见上文代码 | `wave capture <sec> --output <out>` |

- [ ] **Step 4: 运行确认通过**

Run: `pnpm test`
Expected: PASS。

- [ ] **Step 5: 类型检查**

Run: `pnpm exec tsc --noEmit -p tsconfig.json`
Expected: 无错误（若有 `defineTool` 类型不匹配，按 `<DSH>/docs/cookbook/adding-a-tool.md` 的最小示例修正参数/输出 schema 声明）。

- [ ] **Step 6: 提交**

```bash
git -C D:/2_xundoc/project/mstudio add dsh-integration
git -C D:/2_xundoc/project/mstudio commit -m "feat(dsh): add passive aitrace tools (shell/wave/serial/map/crash)"
```

### Task 6: 暂停/GDB 工具组 + 审批

**Files:**
- Create: `<PACKAGE>/src/aitrace/intrusive.ts`
- Create: `<PACKAGE>/tests/aitrace/intrusive.test.ts`

- [ ] **Step 1: 写失败测试（审批门禁逻辑）**

`<PACKAGE>/tests/aitrace/intrusive.test.ts`：

```ts
import { describe, expect, it } from 'vitest'
import { gateByApproval } from '../../src/aitrace/intrusive.ts'

describe('gateByApproval', () => {
  it('allows when approval returns allowed-once', async () => {
    const outcome = await gateByApproval(
      { approval: true },
      { request: async () => 'allowed-once' } as never,
      { agent: {} } as never,
      'aitrace_ocd_regs',
      'Pause CPU ~1s to read registers; interrupts the real-time control loop',
    )
    expect(outcome).toBe(true)
  })

  it('blocks when approval returns rejected', async () => {
    const outcome = await gateByApproval(
      { approval: true },
      { request: async () => 'rejected' } as never,
      { agent: {} } as never,
      'aitrace_ocd_halt',
      'reason',
    )
    expect(outcome).toBe(false)
  })

  it('fails closed without an approval service', async () => {
    const outcome = await gateByApproval(
      { approval: true },
      undefined,
      { agent: {} } as never,
      'aitrace_gdb_connect',
      'reason',
    )
    expect(outcome).toBe(false)
  })

  it('passes through when approval config is disabled', async () => {
    const outcome = await gateByApproval({ approval: false }, undefined, { agent: {} } as never, 'x', 'y')
    expect(outcome).toBe(true)
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `pnpm test`
Expected: FAIL，模块不存在。

- [ ] **Step 3: 实现 intrusive.ts**

```ts
import type { Context } from '@deepseek-ai/cordis'
import { defineTool } from '@deepseek-ai/dsh-tools'
import type { Agent } from '@deepseek-ai/dsh-agent'
import type { ApprovalService } from '@deepseek-ai/dsh-user-approval'
import type { AitraceConfigT } from './config.ts'
import { runAitrace } from './runner.ts'

export async function gateByApproval(
  cfg: AitraceConfigT,
  approval: ApprovalService | undefined,
  agent: Agent,
  toolName: string,
  reason: string,
): Promise<boolean> {
  if (!cfg.approval) return true
  if (!approval) return false // fail closed
  const outcome = await approval.request({ agent, toolName, reason })
  return outcome === 'allowed-once'
}

export function registerIntrusiveAitraceTools(ctx: Context, cfg: AitraceConfigT): void {
  const approval = ctx.get('approval') as ApprovalService | undefined
  const intrusive = (toolName: string, subcommand: string, description: string, parameters: object, schema: object) =>
    ctx.tools.register(defineTool({
      name: toolName,
      description: description + ' INTRUSIVE: pauses the CPU (interrupts real-time control loops); requires engineer approval each call.',
      parameters: parameters as never,
      output: { schema: schema as never, render: (_a, v) => [{ type: 'text', text: String(v) }] },
      async execute(args: never, exec) {
        const agent = exec.agent as Agent
        const ok = await gateByApproval(cfg, approval, agent, toolName, description)
        if (!ok) {
          throw new Error(
            `Approval rejected for ${toolName}: the MCU was NOT paused. Explain to the engineer why you need it and wait for explicit consent.`,
          )
        }
        const r = await runAitrace(ctx, cfg, subcommand.split(' ').concat(subcommandArgs(args)), { signal: exec.signal })
        if (r.exitCode !== 0) throw new Error(`${toolName} failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
        return r.stdout
      },
    }))
  // subcommandArgs maps each tool's typed args to CLI tokens; implemented per tool below.

  intrusive('aitrace_ocd_regs', 'ocd regs', 'Read all core registers (R0-R12, SP, LR, PC, xPSR).', {}, { type: 'string' })
  intrusive('aitrace_ocd_peek', 'ocd peek', 'Passive-ish memory read of one uint32 at address.', { address: { type: 'string', required: true } }, { type: 'string' })
  intrusive('aitrace_ocd_mdw', 'ocd mdw', 'Dump N words from SRAM.', { address: { type: 'string', required: true }, count: { type: 'number' } }, { type: 'string' })
  intrusive('aitrace_ocd_stack', 'ocd stack', 'Dump N words around SP.', { count: { type: 'number' } }, { type: 'string' })
  intrusive('aitrace_ocd_halt', 'ocd halt', 'Halt the CPU and keep it halted.', {}, { type: 'string' })
  intrusive('aitrace_ocd_resume', 'ocd resume', 'Resume the CPU.', {}, { type: 'string' })
  intrusive('aitrace_gdb_connect', 'gdb connect', 'Attach GDB with the project ELF.', { elf: { type: 'string' } }, { type: 'string' })
  intrusive('aitrace_gdb_break', 'gdb break', 'Set a breakpoint at a location.', { location: { type: 'string', required: true } }, { type: 'string' })
  intrusive('aitrace_gdb_continue', 'gdb continue', 'Continue execution.', {}, { type: 'string' })
  intrusive('aitrace_gdb_step', 'gdb step', 'Single-step.', {}, { type: 'string' })
  intrusive('aitrace_gdb_print', 'gdb print', 'Print a variable or expression.', { expression: { type: 'string', required: true } }, { type: 'string' })
  intrusive('aitrace_gdb_bt', 'gdb bt', 'Backtrace.', {}, { type: 'string' })
  intrusive('aitrace_gdb_detach', 'gdb detach', 'Detach GDB.', {}, { type: 'string' })
}
```

`subcommandArgs` 为把各工具 args 映射为 CLI token 的纯函数，映射表：

| 工具 | args → CLI tokens |
|---|---|
| `aitrace_ocd_peek` | `[address]`（原样 hex 字符串） |
| `aitrace_ocd_mdw` | `[address]` + （count 有值时 `[String(count)]`） |
| `aitrace_ocd_stack` | count 有值时 `[String(count)]`，否则 `[]` |
| `aitrace_ocd_halt/resume`、`aitrace_gdb_continue/step/bt/detach` | `[]` |
| `aitrace_gdb_connect` | `--elf <elf>`（默认 `cfg.elfPath`） |
| `aitrace_gdb_break` | `[location]` |
| `aitrace_gdb_print` | `[expression]` |

按此表补 2 个 argv 用例（peek 参数拼接、gdb connect elf 默认值）。

- [ ] **Step 4: 运行确认通过**

Run: `pnpm test`
Expected: PASS（4 + 2 用例）。

- [ ] **Step 5: 提交**

```bash
git -C D:/2_xundoc/project/mstudio add dsh-integration
git -C D:/2_xundoc/project/mstudio commit -m "feat(dsh): add intrusive aitrace tools gated by per-call approval"
```

### Task 7: 域 A 端到端挂载验证

- [ ] **Step 1: 更新聚合入口，注册两组工具**

`<PACKAGE>/src/index.ts`：用 `AitraceConfig` schema 替换 `Config = undefined as never`（`z.infer` 类型），`apply()` 中按需加载 `registerPassiveAitraceTools` 与 `registerIntrusiveAitraceTools`（`inject` 增加 `'approval'` 以直接拿到服务实例；若该服务未装则用 `ctx.get('approval')` 并容错）。重新 `pnpm run build`。

- [ ] **Step 2: 重启 DSH web 并确认工具可见**

重启 `pnpm dsh --profile web`（或触发 HMR），新开会话，输入：`列出你能用的 aitrace 工具`。
Expected: 模型列出 `aitrace_shell`、`aitrace_wave_stat`、`aitrace_wave_capture`、`aitrace_ocd_*`、`aitrace_gdb_*` 等并说明侵入分级。

- [ ] **Step 3: 审批路径人工验证**

（硬件就绪时）输入：`用 aitrace_ocd_regs 读一下寄存器`。
Expected: 会话弹出审批询问（"aitrace_ocd_regs 将暂停 CPU 约 1 秒（中断实时控制环），允许？"）；拒绝后工具返回明确错误，模型解释原因。

- [ ] **Step 4: 提交**（仅当有改动）

---

## Phase 2：域 B kicad-auditor（M3，约 1 天）

### Task 8: 构建 kicad-auditor 并跑通自测

- [ ] **Step 1: 确认/安装 clang++（MSYS2）**

Run: `D:\0_software\msys64\usr\bin\bash.exe -lc "pacman -Q mingw-w64-x86_64-clang 2>/dev/null || pacman -S --noconfirm mingw-w64-x86_64-clang"`
Expected: clang++ 可用（`clang++ --version` 输出 ≥20）。若 pacman 网络受限，改用已装工具链；Makefile 里 `CXX` 指向实际路径。

- [ ] **Step 2: 构建并跑自测**

Run: `cd D:/2_xundoc/project/kicad-auditor && .\make.bat`
Expected: 生成 `kicad-auditor.exe`；输出包含 79 项 PASS 的自测结果。若失败，先修 Makefile（CXX 路径/flags），不绕过测试。

- [ ] **Step 3: 用 Buck 仿真工程验证 sch/param/run**

Run: `.\kicad-auditor.exe sch -i "D:\2_xundoc\project\circuit\KiCad-Simulations\Buck\<sch>.kicad_sch" -j`
Expected: 输出 JSON（components + violations）。`param <ref> <sch> -j` 对 Buck 的反馈芯片/分压电阻输出引脚级网络连接。
若 Buck 工程无 FB 网络（纯仿真），改用 `boost-complete` 或 `smps-com`（含反馈的工程）验证。

- [ ] **Step 4: 记录验证结果到 `<PACKAGE>/docs/kicad-auditor-verified.md`**（工程路径、命令、输出样例）

- [ ] **Step 5: 提交**（kicad-auditor 仓库内提交构建修复；mstudio 仓库提交验证文档）

### Task 9: audit_* 工具封装

**Files:**
- Create: `<PACKAGE>/src/kicad/audit.ts`
- Create: `<PACKAGE>/tests/kicad/audit.test.ts`

- [ ] **Step 1: 写失败测试（JSON 解析与 elf 默认值）**

`<PACKAGE>/tests/kicad/audit.test.ts`：测试 `parseAuditJson`（把 kicad-auditor `-j` stdout 解析为 `{violations, components}`，容忍尾部日志）与 `buildAuditArgv`（audit_sch/audit_param/audit_pcb/audit_run 的 argv 构造，auditor 路径与工作目录相对化）。

- [ ] **Step 2: 运行确认失败** → `pnpm test` FAIL。

- [ ] **Step 3: 实现 audit.ts**

4 个工具，模式同 Task 5：
- `audit_sch`：`<auditorPath> sch -i <sch> -j`，返回 `{violations, components}`；无 `-j` 时返回文本；
- `audit_param`：`<auditorPath> param <ref> <sch> -j`，返回 `{pins: [{pin_num, pin_name, net_name, other_connections}], nearby}`；
- `audit_pcb`：`<auditorPath> pcb -i <pcb> -j`，返回文本/违规列表；
- `audit_run`：`<auditorPath> run -i <pcb> [-c <mm>] [-o <md>]`，返回报告路径。

Config 扩展（`AitraceConfig` 改名或并列 `KicadConfig`）：`auditorPath`（默认 `D:/2_xundoc/project/kicad-auditor/kicad-auditor.exe`）。subprocess 用法复用 Task 4 的 `runAitrace`（改名为通用 `runTool` 或新增 `runAuditor`——选后者，避免动已测代码）。

- [ ] **Step 4: 运行确认通过** → `pnpm test` PASS。
- [ ] **Step 5: 提交**：`feat(dsh): add kicad-auditor tools (audit_sch/param/pcb/run)`

---

## Phase 3：域 B 计算/绘制/MCP（M4，约 1 天）

### Task 10: circuit_calc 工具

**Files:**
- Create: `<PACKAGE>/src/kicad/calc.ts`
- Create: `<PACKAGE>/tests/kicad/calc.test.ts`

- [ ] **Step 1: 写失败测试（FB 分压 + 误差带 + buck 纹波）**

`<PACKAGE>/tests/kicad/calc.test.ts`：

```ts
import { describe, expect, it } from 'vitest'
import { fbDivider, buckInductorRipple } from '../../src/kicad/calc.ts'

describe('fbDivider', () => {
  it('computes Vout from VREF and divider with tolerance band', () => {
    const r = fbDivider({ vref: 0.6, rTop: 10000, rBottom: 4700, tolerance: 0.01, fbCurrent: 0 })
    expect(r.voutNominal).toBeCloseTo(1.877, 2)
    expect(r.voutMin).toBeLessThan(r.voutNominal)
    expect(r.voutMax).toBeGreaterThan(r.voutNominal)
  })

  it('applies FB bias current correction', () => {
    const r = fbDivider({ vref: 0.6, rTop: 10000, rBottom: 4700, tolerance: 0.01, fbCurrent: 0.5e-6 })
    expect(r.voutNominal).toBeCloseTo(1.877 - 0.6 * (10000 / 4700) * 0.5e-6 * 4700, 2)
  })

  it('solves for a target Vout by picking a standard R', () => {
    const r = fbDivider({ vref: 0.6, rTop: 10000, rBottom: 4700, tolerance: 0.01, fbCurrent: 0, targetVout: 1.8 })
    expect(r.recommendedBottom).toBeGreaterThan(0)
    expect(r.voutWithRecommended).toBeLessThan(1.9)
    expect(r.voutWithRecommended).toBeGreaterThan(1.7)
  })
})

describe('buckInductorRipple', () => {
  it('computes ripple current of a buck inductor', () => {
    const r = buckInductorRipple({ vin: 12, vout: 3.3, fsw: 500e3, l: 10e-6 })
    expect(r.dIL).toBeCloseTo((12 - 3.3) * 3.3 / 12 / 500e3 / 10e-6, 6)
  })
})
```

- [ ] **Step 2: 运行确认失败** → FAIL。
- [ ] **Step 3: 实现 calc.ts**

纯函数：`fbDivider`（E96 序列里为 targetVout 找最近标准值，输出 nominal/min/max/recommended）、`buckInductorRipple`、`rcCutoff`、`sallenKeyGain`。注册工具 `circuit_calc`：参数为计算类型（`fb-divider | buck-ripple | rc-cutoff | sallen-key`）+ 对应参数对象（union schema，`exact-one` 由 defineTool 支持），返回结构化结果。计算逻辑全部为纯函数（可测），工具只是参数通道。

- [ ] **Step 4: 运行确认通过** → PASS。
- [ ] **Step 5: 提交**：`feat(dsh): add circuit_calc tool with FB divider and buck ripple math`

### Task 11: SVG 拓扑图工具

**Files:**
- Create: `<PACKAGE>/src/kicad/svg.ts`
- Create: `<PACKAGE>/tests/kicad/svg.test.ts`

- [ ] **Step 1: 写失败测试（SVG 骨架与坐标转义）**

测试 `wrapSvg`（生成含 viewBox 的完整 SVG 文档字符串，宽度/高度参数化）与 `escapeSvgText`（`& < > " '` 转义）。

- [ ] **Step 2: 运行确认失败** → FAIL。
- [ ] **Step 3: 实现 svg.ts**

注册工具 `svg_topology`：参数 `kind: 'buck' | 'boost' | 'fb-divider' | 'rc-filter' | 'sallen-key'` + `params`（各拓扑参数）。实现为**参数化的 SVG 模板生成器**（buck：输入/开关/电感/二极管/输出电容/FB 分压标值；fb-divider：VIN→R1→FB→R2→GND 标注 VREF 与计算 Vout），返回 `{svgPath}` 落盘到工作目录 `diagrams/`，render 输出 markdown 图片引用（`![](相对路径)`，供 Web GUI 渲染）。拓扑数据由 AI 从 `circuit_calc` 结果与 `audit_param` 提取的真实值填入。

- [ ] **Step 4: 运行确认通过** → PASS。
- [ ] **Step 5: 提交**：`feat(dsh): add svg_topology tool for in-session circuit diagrams`

### Task 12: 华秋 KiCad MCP 接入

- [ ] **Step 1: 验证 kicad-mcp-server 可启动并确认 socket URL 来源**

启动 KiCad（华秋版 9.0.7），观察其 copilot/SDK 服务是否自动监听 nng socket；用 README 的命令试跑：
`D:\0_software\KiCad\9.0\bin\uv.exe --directory D:\0_software\KiCad\9.0\bin\kicad-mcp-server run main.py ipc:///tmp/kicad_copilot_pair-<id>.ipc`
Expected: server 启动、stdio 就绪。若 socket URL 未知，检查 `%APPDATA%\kicad\copilot\site_env.json` 与 KiCad 进程命令行/日志；若华秋版在 KiCad 内提供"AI 协同"入口显示连接地址，从该入口取。**此步产出：socket URL 获取 SOP 写入 `<PACKAGE>/docs/kicad-mcp-connect.md`**。若 pynng 依赖缺失（README 提到清华镜像 403），设 `UV_DEFAULT_INDEX=https://pypi.org/simple` 或指定可用镜像后重试。

- [ ] **Step 2: 配置 dsh-mcp-client**

在 DSH profile patch（`<PACKAGE>/cordis.patch.yml`）追加：

```yaml
- insert:
    - id: mcp-kicad
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        serverName: kicad
        command: D:/0_software/KiCad/9.0/bin/uv.exe
        args:
          - --directory
          - D:/0_software/KiCad/9.0/bin/kicad-mcp-server
          - run
          - main.py
          - ipc:///tmp/kicad_copilot_pair-<id>.ipc
```

（command/args 键名以 `<DSH>/packages/mcp/mcp-client/README.md` 的配置示例为准；`<id>` 用 Step 1 确认的实际 socket。）

- [ ] **Step 3: 确认工具注册与命名**

重启 DSH web，新开会话输入：`列出 kicad 相关的 MCP 工具`。
Expected: 出现 `mcp__kicad__get_netlist`、`mcp__kicad__query_symbol_library` 等只读工具与 `mcp__kicad__place_symbol` 等写工具。

- [ ] **Step 4: 写工具审批策略（tools/pre-execute）**

在 `<PACKAGE>/src/index.ts` 注册 waterfall 监听：`mcp__kicad__*` 中写操作（名称匹配 `place_|draw_|create_|modify_|move_|rotate_|set_|delete_|save`）→ 调 `ctx.approval.request`（与 Task 6 相同门禁，复用 `gateByApproval`）；只读（`get_|query_|export_|open_|close_|run_|show_`）放行。拒绝 → 返回明确错误。测试：单测 gate 的匹配函数（`isKicadWriteTool`），断言 8 个写/8 个读样例。

- [ ] **Step 5: 提交**：`feat(dsh): wire Huaqiu KiCad MCP with write-tool approval policy`

---

## Phase 4：端到端验证（M5，半天）

### Task 13: S1 波形链路质量验收

- [ ] **Step 1: 硬件就绪**（OpenOCD + 探针 + 目标板；`tasklist | findstr openocd` 确认）。
- [ ] **Step 2: 会话验证**：新会话（cwd=modus_template），输入：`验收一下波形链路`。
Expected: 模型自动加载 aitrace skill → 查 OpenOCD → `aitrace_wave_stat 5` → 对照标准（rate≈1000、crc=0、seq_lost=0）→ 交叉验证 `aitrace_shell` 的 `wave drop` → 输出结论。
- [ ] **Step 3: 记录结果**到 `<PACKAGE>/docs/s1-wave-link-verification.md`。

### Task 14: S2 FB 分压校验

- [ ] **Step 1: 选取含 FB 网络的工程**（如 `KiCad-Simulations/boost-complete` 或用户实际板子），确认 sch 路径。
- [ ] **Step 2: 会话验证**：输入：`校验这个工程的电源 FB 分压`。
Expected: `audit_sch -j` 定位 FB 网络 → `audit_param <ref>` 提取 R1/R2 → `circuit_calc fb-divider` 出 Vout±误差 → 建议 + 可选 SVG 拓扑。
- [ ] **Step 3: 记录结果**到 `<PACKAGE>/docs/s2-fb-divider-verification.md`。

### Task 15: S3 全板审计 + notes 沉淀

- [ ] **Step 1: 会话验证**：输入：`跑一下这个板的全板审计`。
Expected: `audit_run`（或 audit_sch+audit_pcb）→ 按严重级输出改板建议清单。
- [ ] **Step 2: notes 机制验证**：会话结束时 AI 在工程 `.agents/notes/` 写入本次结论；新会话提问 `上次审计的结论是什么`，AI 从 notes 恢复。
- [ ] **Step 3: 记录结果**到 `<PACKAGE>/docs/s3-audit-verification.md`。

### Task 16: 使用手册与收尾

**Files:**
- Create: `<PACKAGE>/README.md`（安装：pnpm install/build、profile patch 挂载、KiCad MCP socket SOP 引用、配置表、三个验证场景的快捷指令）
- Modify: `D:\2_xundoc\project\mstudio\README.md`（索引链接到 dsh-integration）

- [ ] **Step 1: 写 README**（含已知限制：KiCad 必须运行中才有 MCP；MCP 绘制默认关闭；写入审批默认开）。
- [ ] **Step 2: 对照 spec 验收清单逐项自检**（spec §8 六条），记录到 `<PACKAGE>/docs/acceptance.md`。
- [ ] **Step 3: 提交**：`docs: add mstudio-dsh user guide and acceptance record`

---

## 依赖顺序与并行

- Task 1-2 可先行（不依赖代码）；
- Task 3-6 顺序；Task 7 依赖 3-6 且需要 DSH 重启验证；
- Task 8 可与 3-6 并行（构建独立）；Task 9 依赖 8；
- Task 10-11 相互独立、依赖 Task 3 骨架；Task 12 依赖 3 与硬件无关但依赖 KiCad 运行验证；
- Task 13-15 依赖 7/9/10/11/12。

## 风险与对策

| 风险 | 对策 |
|---|---|
| `ctx.subprocess.collected` API 命名与计划不符 | Task 4 以 types.ts 为准修正并保持测试覆盖 |
| pynng 依赖 403（清华镜像） | Step 12.1 换镜像/UV_DEFAULT_INDEX |
| 华秋 MCP socket URL 获取方式未知 | Step 12.1 产出 SOP；不通则降级：域 B 只用离线通道（auditor+calc+svg） |
| clang++ 不在 MSYS2 | Task 8 pacman 安装或改 Makefile 指向已有工具链 |
| DSH skill 项目根识别偏差 | Task 2 用日志确认 `.git` 祖先判定 |
