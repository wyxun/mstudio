import type { Agent } from '@deepseek-ai/dsh-agent'
import type { ApprovalService, ApprovalRequest } from '@deepseek-ai/dsh-user-approval'
import type { AitraceConfigT } from './types.ts'

/**
 * Per-call approval gate for intrusive (halt/GDB) aitrace tools.
 * Returns true when the engineer allowed the specific call. Fails closed:
 * a missing approval service or a rejected/cancelled/unavailable request
 * denies execution.
 */
export async function gateByApproval(
  cfg: AitraceConfigT,
  approval: ApprovalService | undefined,
  agent: Agent,
  toolName: string,
  reason: string,
): Promise<boolean> {
  if (!cfg.approval) return true
  if (!approval) return false
  const req: ApprovalRequest = { agent, toolName, reason }
  const outcome = await approval.request(req)
  return outcome === 'allowed-once'
}

/**
 * Map typed tool args to aitrace CLI tokens for intrusive subcommands.
 * Each tool declares its own args shape; this switch keeps the mapping
 * explicit and testable.
 */
export function buildIntrusiveArgv(toolName: string, cfg: AitraceConfigT, args: Record<string, unknown>): string[] {
  switch (toolName) {
    case 'aitrace_ocd_regs': return ['ocd', 'regs']
    case 'aitrace_ocd_peek': return ['ocd', 'peek', String(args.address)]
    case 'aitrace_ocd_mdw': {
      const tokens = ['ocd', 'mdw', String(args.address)]
      if (args.count !== undefined) tokens.push(String(args.count))
      return tokens
    }
    case 'aitrace_ocd_stack': return args.count !== undefined ? ['ocd', 'stack', String(args.count)] : ['ocd', 'stack']
    case 'aitrace_ocd_halt': return ['ocd', 'halt']
    case 'aitrace_ocd_resume': return ['ocd', 'resume']
    case 'aitrace_gdb_connect': return ['gdb', 'connect', '--elf', String(args.elf ?? cfg.elfPath)]
    case 'aitrace_gdb_break': return ['gdb', 'break', String(args.location)]
    case 'aitrace_gdb_continue': return ['gdb', 'continue']
    case 'aitrace_gdb_step': return ['gdb', 'step']
    case 'aitrace_gdb_print': return ['gdb', 'print', String(args.expression)]
    case 'aitrace_gdb_bt': return ['gdb', 'bt']
    case 'aitrace_gdb_detach': return ['gdb', 'detach']
    default: throw new Error(`unknown intrusive tool: ${toolName}`)
  }
}
