import z from '@deepseek-ai/schemastery'
import type { AitraceConfigT } from './types.ts'

/** Plugin configuration schema for the aitrace tool family. */
export const AitraceConfig: z<AitraceConfigT> = z.object({
  /** Repository root of the firmware project; every relative path resolves here. */
  workDir: z.string().default('D:/2_xundoc/project/modus_template'),
  /** Path to aitrace.exe, absolute or relative to workDir. */
  aitracePath: z.string().default('tools/aitrace.exe'),
  /** ELF used by map/crash/gdb when the model does not pass one. */
  elfPath: z.string().default('build/template.elf'),
  /** Linker map used by map info. */
  mapPath: z.string().default('build/template.map'),
  /** Directory (relative to workDir) for wave captures. */
  captureDir: z.string().default('captures'),
  /** Master switch for halt/GDB per-call approval; false bypasses the gate. */
  approval: z.boolean().default(true),
  /** Fallback per-invocation timeout in milliseconds. */
  shellTimeoutMs: z.number().default(15000),
})
