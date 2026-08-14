import type { Context } from '@deepseek-ai/cordis'
import { defineTool } from '@deepseek-ai/dsh-tools'
import type { Agent } from '@deepseek-ai/dsh-agent'
import type { ApprovalService } from '@deepseek-ai/dsh-user-approval'
import type { AitraceConfigT } from './types.ts'
import { runAitrace } from './runner.ts'
import { gateByApproval, buildIntrusiveArgv } from './gate.ts'

interface IntrusiveToolSpec {
  name: string
  description: string
  parameters: Record<string, unknown>
}

const INTRUSIVE_TOOLS: IntrusiveToolSpec[] = [
  {
    name: 'aitrace_ocd_regs',
    description: 'Read all core registers (R0-R12, SP, LR, PC, xPSR) via OpenOCD. PAUSES the CPU ~1s.',
    parameters: {},
  },
  {
    name: 'aitrace_ocd_peek',
    description: 'Read one uint32 at a memory address via OpenOCD. PAUSES the CPU ~1s.',
    parameters: { address: { type: 'string', required: true, description: 'Hex address, e.g. "0x20000000"' } },
  },
  {
    name: 'aitrace_ocd_mdw',
    description: 'Dump N words of memory via OpenOCD. PAUSES the CPU ~1s.',
    parameters: {
      address: { type: 'string', required: true, description: 'Hex address' },
      count: { type: 'number', description: 'Word count (default 16)' },
    },
  },
  {
    name: 'aitrace_ocd_stack',
    description: 'Dump words around SP via OpenOCD. PAUSES the CPU ~1s.',
    parameters: { count: { type: 'number', description: 'Word count (default 32)' } },
  },
  {
    name: 'aitrace_ocd_halt',
    description: 'Halt the CPU and keep it halted. Interrupts real-time control loops until resume.',
    parameters: {},
  },
  {
    name: 'aitrace_ocd_resume',
    description: 'Resume a halted CPU.',
    parameters: {},
  },
  {
    name: 'aitrace_gdb_connect',
    description: 'Attach GDB with the project ELF. Full debug control; halts the target.',
    parameters: { elf: { type: 'string', description: 'ELF path; defaults to the configured project ELF' } },
  },
  {
    name: 'aitrace_gdb_break',
    description: 'Set a breakpoint. Halts the target when hit; interrupts real-time control.',
    parameters: { location: { type: 'string', required: true, description: 'Location, e.g. "main.c:100"' } },
  },
  {
    name: 'aitrace_gdb_continue',
    description: 'Continue execution after a stop.',
    parameters: {},
  },
  {
    name: 'aitrace_gdb_step',
    description: 'Single-step the target.',
    parameters: {},
  },
  {
    name: 'aitrace_gdb_print',
    description: 'Print a variable or expression via GDB.',
    parameters: { expression: { type: 'string', required: true, description: 'Expression, e.g. "g_wTickCounter"' } },
  },
  {
    name: 'aitrace_gdb_bt',
    description: 'Print the backtrace.',
    parameters: {},
  },
  {
    name: 'aitrace_gdb_detach',
    description: 'Detach GDB and resume the target.',
    parameters: {},
  },
]

const renderText = (_args: never, value: unknown) => [{ type: 'text' as const, text: String(value) }]

/**
 * Register intrusive aitrace tools (ocd halt-based and gdb). Every call goes
 * through the approval gate first; a denial returns an explicit error and the
 * MCU is NOT touched.
 */
export function registerIntrusiveAitraceTools(ctx: Context, cfg: AitraceConfigT): void {
  const approval = ctx.get('approval') as ApprovalService | undefined
  for (const spec of INTRUSIVE_TOOLS) {
    ctx.tools.register(defineTool({
      name: spec.name,
      description: spec.description
        + ' INTRUSIVE: pauses the CPU (interrupts real-time control loops); requires engineer approval each call.',
      parameters: spec.parameters as never,
      output: {
        schema: { type: 'string' },
        render: renderText,
      },
      async execute(args: never, exec) {
        const agent = exec.agent as Agent
        const ok = await gateByApproval(cfg, approval, agent, spec.name, spec.description)
        if (!ok) {
          throw new Error(
            `Approval rejected for ${spec.name}: the MCU was NOT paused. Explain to the engineer why you need it and wait for explicit consent.`,
          )
        }
        const argv = buildIntrusiveArgv(spec.name, cfg, args as Record<string, unknown>)
        const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
        if (r.exitCode !== 0) throw new Error(`${spec.name} failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
        return r.stdout
      },
    }))
  }
}
