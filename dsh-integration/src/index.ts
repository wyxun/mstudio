// Aggregated plugin entry for the mstudio-dsh bundle.
// Registers the aitrace tool families (passive + intrusive) for MCU debugging
// through the modus_template firmware project's OpenOCD RTT link.
import type { Context } from '@deepseek-ai/cordis'
import z from '@deepseek-ai/schemastery'
import type { AitraceConfigT } from './aitrace/types.ts'
import { registerPassiveAitraceTools } from './aitrace/passive.ts'
import { registerIntrusiveAitraceTools } from './aitrace/intrusive.ts'

export const name = 'mstudio-dsh'
export const inject = ['tools', 'subprocess']

/** Plugin configuration; schema in Config validates every field. */
export interface Config extends AitraceConfigT {}

export const Config: z<Config> = z.object({
  workDir: z.string().default('D:/2_xundoc/project/modus_template'),
  aitracePath: z.string().default('tools/aitrace.exe'),
  elfPath: z.string().default('build/template.elf'),
  mapPath: z.string().default('build/template.map'),
  captureDir: z.string().default('captures'),
  approval: z.boolean().default(true),
  shellTimeoutMs: z.number().default(15000),
})

export function apply(ctx: Context, config: Config): void {
  registerPassiveAitraceTools(ctx, config)
  registerIntrusiveAitraceTools(ctx, config)
}
